#include "outlining/Funcs.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/FunctionAttrs.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <string>
#include <vector>

#include "bcdb/AlignBitcode.h"
#include "bcdb/Context.h"
#include "bcdb/Split.h"
#include "memodb/Evaluator.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"
#include "memodb/Store.h"
#include "outlining/Candidates.h"
#include "outlining/Dependence.h"
#include "outlining/Extractor.h"
#include "outlining/FalseMemorySSA.h"
#include "outlining/LinearProgram.h"
#include "outlining/SizeModel.h"

using namespace llvm;
using namespace memodb;
using bcdb::Context;
using bcdb::getSoleDefinition;
using bcdb::LinearProgram;
using bcdb::OutliningCalleeExtractor;
using bcdb::OutliningCallerExtractor;
using bcdb::OutliningCandidates;
using bcdb::OutliningCandidatesAnalysis;
using bcdb::OutliningCandidatesOptions;
using bcdb::OutliningDependenceAnalysis;
using bcdb::SizeModelAnalysis;
using bcdb::SizeModelResults;

const char *smout::actual_size_version = "smout.actual_size_v0";
const char *smout::candidates_version = "smout.candidates_v2";
const char *smout::grouped_candidates_version = "smout.grouped_candidates_v2";
const char *smout::extracted_callees_version = "smout.extracted_callees_v4";
const char *smout::grouped_callees_for_function_version =
    "smout.grouped_callees_for_function_v4";
const char *smout::grouped_callees_version = "smout.grouped_callees_v4";
const char *smout::ilp_problem_version = "smout.ilp_problem";
const char *smout::greedy_solution_version = "smout.greedy_solution_v5";
const char *smout::extracted_caller_version = "smout.extracted_caller_v4";
const char *smout::outlined_module_version = "smout.outlined_module_v0";
const char *smout::optimized_version = "smout.optimized_v6";
const char *smout::refinements_for_set_version = "smout.refinements_for_set_v0";
const char *smout::refinements_for_group_version =
    "smout.refinements_for_group_v2";
const char *smout::grouped_refinements_version = "smout.grouped_refinements_v6";

static FunctionAnalysisManager makeFAM(const Node &options) {
  OutliningCandidatesOptions cand_opts;
  cand_opts.max_adjacent =
      options.get_value_or<size_t>("max_adjacent", cand_opts.max_adjacent);
  cand_opts.max_args =
      options.get_value_or<size_t>("max_args", cand_opts.max_args);
  cand_opts.max_nodes =
      options.get_value_or<size_t>("max_nodes", cand_opts.max_nodes);
  cand_opts.min_caller_savings =
      options.get_value_or<int>("min_rough_caller_savings", 1);

  FunctionAnalysisManager am;
  // TODO: Would it be faster to just register the analyses we need?
  PassBuilder().registerFunctionAnalyses(am);
  am.registerPass([] { return FalseMemorySSAAnalysis(); });
  am.registerPass([=] { return OutliningCandidatesAnalysis(cand_opts); });
  am.registerPass([] { return OutliningDependenceAnalysis(); });
  am.registerPass([] { return SizeModelAnalysis(); });
  return am;
}

static void postprocessModule(Module &m) {
  LoopAnalysisManager lam;
  FunctionAnalysisManager fam;
  CGSCCAnalysisManager cgam;
  ModuleAnalysisManager mam;
  PassBuilder pb;
  pb.crossRegisterProxies(lam, fam, cgam, mam);
  pb.registerLoopAnalyses(lam);
  pb.registerFunctionAnalyses(fam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerModuleAnalyses(mam);
  ModulePassManager pm;
  pm.addPass(createModuleToFunctionPassAdaptor(SimplifyCFGPass()));
  pm.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));
  pm.run(m, mam);
}

static StringRef cid_key(const CID &cid) {
  auto bytes = cid.asBytes();
  return StringRef(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

static Node encodeBitVector(const SparseBitVector<> &bv) {
  // Most bitvectors just have one contiguous range of set bits. So we encode
  // just the indices where we change between set and clear.
  Node result(node_list_arg);
  size_t end = 0;
  for (auto start : bv) {
    if (start < end)
      continue;
    for (end = start + 1; bv.test(end); ++end) {
    }
    result.emplace_back(start);
    result.emplace_back(end);
  }
  return result;
}

static SparseBitVector<> decodeBitVector(const Node &node) {
  SparseBitVector<> bv;
  for (size_t i = 0; i < node.size(); i += 2) {
    size_t start = node[i].as<size_t>(), end = node[i + 1].as<size_t>();
    for (size_t j = start; j < end; j++)
      bv.set(j);
  }
  return bv;
}

static Node getTypeName(const Type *type) {
  // TODO: cache results.

  enum class Primitive : int {
    // General primitive types.
    Void = 0,
    Label = 1,
    Metadata = 2,
    Token = 3,
    OpaqueStruct = 4,

    // IEEE floats.
    Half = 11,
    Float = 12,
    Double = 13,
    FP128 = 14,

    // Target-specific primitive types.
    X86_FP80 = -1,
    PPC_FP128 = -2,
    X86_MMX = -3,
    BFloat = -4,
    X86_AMX = -5,
  };

  enum class Derived : int {
    // Without subtypes (we ignore pointer element types).
    Integer = 0,
    Pointer = 1,

    // With subtypes.
    Array = -1,
    FixedVector = -2,
    ScalableVector = -3,
    Struct = -4,
    Function = -5,
  };

  Node subtypes(node_list_arg);
  if (!type->isPointerTy())
    for (const Type *subtype : type->subtypes())
      subtypes.push_back(getTypeName(subtype));

  switch (type->getTypeID()) {
  case Type::VoidTyID:
    return static_cast<int>(Primitive::Void);
  case Type::LabelTyID:
    return static_cast<int>(Primitive::Label);
  case Type::MetadataTyID:
    return static_cast<int>(Primitive::Metadata);
  case Type::TokenTyID:
    return static_cast<int>(Primitive::Token);
  case Type::HalfTyID:
    return static_cast<int>(Primitive::Half);
  case Type::FloatTyID:
    return static_cast<int>(Primitive::Float);
  case Type::DoubleTyID:
    return static_cast<int>(Primitive::Double);
  case Type::FP128TyID:
    return static_cast<int>(Primitive::FP128);
  case Type::X86_FP80TyID:
    return static_cast<int>(Primitive::X86_FP80);
  case Type::PPC_FP128TyID:
    return static_cast<int>(Primitive::PPC_FP128);
  case Type::X86_MMXTyID:
    return static_cast<int>(Primitive::X86_MMX);
#if LLVM_VERSION_MAJOR >= 11
  case Type::BFloatTyID:
    return static_cast<int>(Primitive::BFloat);
#endif
#if LLVM_VERSION_MAJOR >= 12
  case Type::X86_AMXTyID:
    return static_cast<int>(Primitive::X86_AMX);
#endif

  case Type::IntegerTyID:
    return Node(node_list_arg, {static_cast<int>(Derived::Integer),
                                type->getIntegerBitWidth()});

  case Type::PointerTyID:
    return Node(node_list_arg, {static_cast<int>(Derived::Pointer),
                                type->getPointerAddressSpace()});

  case Type::ArrayTyID:
    return Node(node_list_arg,
                {static_cast<int>(Derived::Array), std::move(subtypes),
                 type->getArrayNumElements()});

#if LLVM_VERSION_MAJOR >= 11
  case Type::FixedVectorTyID:
    return Node(node_list_arg,
                {static_cast<int>(Derived::FixedVector), std::move(subtypes),
                 cast<FixedVectorType>(type)->getNumElements()});

  case Type::ScalableVectorTyID:
    return Node(node_list_arg,
                {static_cast<int>(Derived::ScalableVector), std::move(subtypes),
                 cast<ScalableVectorType>(type)->getMinNumElements()});
#else
  case Type::VectorTyID:
    return Node(node_list_arg,
                {static_cast<int>(Derived::FixedVector), std::move(subtypes),
                 cast<VectorType>(type)->getNumElements()});
#endif

  case Type::StructTyID:
    if (cast<StructType>(type)->isOpaque())
      return static_cast<int>(Primitive::OpaqueStruct);
    // We don't care about isLiteral().
    return Node(node_list_arg,
                {static_cast<int>(Derived::Struct), std::move(subtypes),
                 cast<StructType>(type)->isPacked()});

  case Type::FunctionTyID:
    return Node(node_list_arg,
                {static_cast<int>(Derived::Function), std::move(subtypes),
                 cast<FunctionType>(type)->isVarArg()});
  }

  report_fatal_error("unsupported Type");
}

static std::string
getGroupName(const OutliningCandidates::Candidate &candidate) {
  Node node(node_list_arg,
            {Node(node_list_arg), Node(node_list_arg), Node(node_map_arg)});
  Node &args = node[0];
  Node &results = node[1];
  Node &globals = node[2];
  for (Type *type : candidate.arg_types)
    args.emplace_back(getTypeName(type));
  for (Type *type : candidate.result_types)
    results.emplace_back(getTypeName(type));
  for (GlobalValue *gv : candidate.globals_used)
    globals[gv->getName()] = nullptr;
  return Multibase::base64pad.encodeWithoutPrefix(node.saveAsCBOR());
}

static bool isGroupWorthExtracting(Link &options, const Node &group) {
  auto min_callee_size = group["min_callee_size"].as<int64_t>();
  auto total_caller_savings = group["total_caller_savings"].as<int64_t>();
  return total_caller_savings > min_callee_size;
}

NodeOrCID smout::actual_size(Evaluator &evaluator, Link func) {
  Context context;
  auto m = cantFail(parseBitcodeFile(
      MemoryBufferRef(func->as<StringRef>(byte_string_arg), ""), context));
  Function &f = getSoleDefinition(*m);
  return Node(SizeModelResults(f).this_function_total_size);
}

NodeOrCID smout::candidates(Evaluator &evaluator, Link options, Link func) {
  ExitOnError Err("smout.candidates: ");
  Context context;
  auto m = Err(parseBitcodeFile(
      MemoryBufferRef(func->as<StringRef>(byte_string_arg), ""), context));
  Function &f = getSoleDefinition(*m);

  FunctionAnalysisManager am = makeFAM(*options);
  auto &candidates = am.getResult<OutliningCandidatesAnalysis>(f);

  Node result(node_map_arg);
  for (const auto &candidate : candidates.Candidates) {
    auto group_name = getGroupName(candidate);
    auto &group = result[group_name];
    if (group.is_null())
      group = Node(node_list_arg);
    group.emplace_back(
        Node(node_map_arg, {
                               {"nodes", encodeBitVector(candidate.bv)},
                               {"callee_size", candidate.callee_size},
                               {"caller_savings", candidate.caller_savings},
                           }));
  }
  return result;
}

NodeOrCID smout::grouped_candidates(Evaluator &evaluator, Link options,
                                    Link mod) {
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync(candidates_version, options, func_cid));
  }
  struct Group {
    int64_t total_caller_savings = 0;
    int64_t min_callee_size = std::numeric_limits<int64_t>::max();
    Node members = Node(node_list_arg);
  };
  StringMap<Group> groups;
  for (auto &func_item : func_candidates) {
    const CID &func_cid = func_item.first;
    Future &result = func_item.second;
    for (auto &item : result->map_range()) {
      Group &group = groups[item.key()];
      for (auto &candidate : item.value().list_range()) {
        group.total_caller_savings += candidate["caller_savings"].as<int64_t>();
        group.min_callee_size = std::min(
            group.min_callee_size, candidate["callee_size"].as<int64_t>());
        Node candidate_changed = candidate;
        candidate_changed["function"] = Node(evaluator.getStore(), func_cid);
        group.members.emplace_back(std::move(candidate_changed));
      }
    }
    result.freeNode();
  }
  Node groups_node(node_map_arg);
  for (auto &item : groups) {
    Group &group = item.getValue();
    groups_node[item.getKey()] =
        Node(node_map_arg,
             {{"total_caller_savings", group.total_caller_savings},
              {"min_callee_size", group.min_callee_size},
              {"num_members", group.members.size()},
              {"members", Node(evaluator.getStore(),
                               evaluator.getStore().put(group.members))}});
  }
  return groups_node;
}

NodeOrCID smout::extracted_callees(Evaluator &evaluator, Link func,
                                   Link node_sets) {
  ExitOnError Err("smout.extracted_callees: ");
  Context context;
  auto m = Err(parseBitcodeFile(
      MemoryBufferRef(func->as<StringRef>(byte_string_arg), ""), context));
  Function &f = getSoleDefinition(*m);
  FunctionAnalysisManager am = makeFAM(Node(node_map_arg));
  auto &deps = am.getResult<OutliningDependenceAnalysis>(f);

  std::vector<Function *> callees;
  for (const auto &nodes : node_sets->list_range()) {
    SparseBitVector<> bv = decodeBitVector(nodes);
    OutliningCalleeExtractor extractor(f, deps, bv);
    callees.push_back(extractor.createDefinition());
  }

  postprocessModule(*m);
  bcdb::Splitter splitter(*m);
  Node result(node_list_arg);
  for (Function *callee : callees) {
    auto size = SizeModelResults(*callee).this_function_total_size;
    auto mpart = splitter.SplitGlobal(callee);
    SmallVector<char, 0> buffer;
    bcdb::WriteAlignedModule(*mpart, buffer);
    result.emplace_back(Node(
        node_map_arg,
        {{"callee", Node(evaluator.getStore(), evaluator.getStore().put(Node(
                                                   byte_string_arg, buffer)))},
         {"size", size}}));
  }
  assert(node_sets->size() == result.size());
  return result;
}

NodeOrCID smout::grouped_callees_for_function(Evaluator &evaluator,
                                              Link options,
                                              Link grouped_candidates,
                                              Link func) {
  auto candidates = evaluator.evaluate(candidates_version, options, func);
  std::vector<std::string> candidate_group;
  std::vector<Node> candidate_node;
  for (auto &item : candidates->map_range()) {
    const auto &group_key = item.key();
    if (!isGroupWorthExtracting(options, (*grouped_candidates)[group_key]))
      continue;
    for (auto &candidate : item.value().list_range()) {
      candidate_group.emplace_back(group_key.str());
      candidate_node.emplace_back(candidate);
    }
  }
  candidates.freeNode();

  // If we only extract one callee at once, we have to run
  // OutliningDependenceAnalysis each time, which is too slow. Instead, extract
  // BATCH_SIZE callees at once.
  static const size_t BATCH_SIZE = 64;
  std::vector<Future> futures;
  for (size_t i = 0; i < candidate_node.size(); i += BATCH_SIZE) {
    Node node_sets(node_list_arg);
    for (size_t j = 0; j < BATCH_SIZE && i + j < candidate_node.size(); ++j)
      node_sets.emplace_back(candidate_node[i + j]["nodes"]);
    futures.emplace_back(
        evaluator.evaluateAsync(extracted_callees_version, func, node_sets));
  }

  Node result(node_map_arg);
  for (size_t i = 0; i < candidate_node.size(); i += BATCH_SIZE) {
    Future &callees = futures[i / BATCH_SIZE];
    for (size_t j = 0; j < BATCH_SIZE && i + j < candidate_node.size(); ++j) {
      Node &group = result[candidate_group[i + j]];
      if (group.is_null())
        group = Node(node_list_arg);
      Node &node = candidate_node[i + j];
      const Node &callee = (*callees)[j];
      node["callee"] = callee["callee"];

      // Update estimated sizes now that we know the real callee size.
      node["estimated_callee_size"] = node["callee_size"];
      node["callee_size"] = callee["size"];
      int delta = node["callee_size"].as<int>() -
                  node["estimated_callee_size"].as<int>();
      if (delta < 0)
        node["caller_savings"] = node["caller_savings"].as<int>() + delta;

      group.emplace_back(node);
    }
  }
  return result;
}

NodeOrCID smout::grouped_callees(Evaluator &evaluator, Link options, Link mod) {
  StringSet original_cids;
  auto grouped_candidates =
      evaluator.evaluate(grouped_candidates_version, options, mod);
  std::vector<std::pair<CID, Future>> futures;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    if (original_cids.insert(cid_key(func_cid)).second)
      futures.emplace_back(
          func_cid,
          evaluator.evaluateAsync(grouped_callees_for_function_version, options,
                                  grouped_candidates, func_cid));
  }
  StringMap<size_t> group_unique_callees;
  StringMap<size_t> group_callees_without_duplicates;
  StringMap<Node> groups;
  for (auto &item : futures) {
    const CID &func_cid = item.first;
    Future &callees_for_function = item.second;
    for (auto &item : callees_for_function->map_range()) {
      auto &group = groups[item.key()];
      StringSet<> unique_callees;
      StringSet<> duplicated_callees;
      if (group.is_null())
        group = Node(node_list_arg);
      for (const auto &candidate : item.value().list_range()) {
        auto key = cid_key(candidate["callee"].as<CID>());
        if (!unique_callees.insert(key).second)
          duplicated_callees.insert(key);
        Node candidate_changed = candidate;
        candidate_changed["function"] = Node(evaluator.getStore(), func_cid);
        group.emplace_back(candidate_changed);
      }
      group_unique_callees[item.key()] = unique_callees.size();
      group_callees_without_duplicates[item.key()] =
          unique_callees.size() - duplicated_callees.size();
    }
  }
  Node result(node_map_arg);
  for (auto &item : groups) {
    Node group = (*grouped_candidates)[item.getKey()];
    group["members"] =
        Node(evaluator.getStore(), evaluator.getStore().put(item.getValue()));
    group["num_unique_callees"] = group_unique_callees[item.getKey()];
    group["num_callees_without_duplicates"] =
        group_callees_without_duplicates[item.getKey()];
    result[item.getKey()] = group;
  }
  return result;
}

NodeOrCID smout::ilp_problem(Evaluator &evaluator, Link options, Link mod) {
  report_fatal_error("This part of BCDB is broken and needs to be updated!");
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync(candidates_version, options, func_cid));
  }

  std::vector<Future> callees;
  LinearProgram problem("SMOUT");
  std::vector<std::optional<LinearProgram::Var>> x_m;
  std::vector<int> s_m, f_m;
  std::vector<std::vector<SmallVector<size_t, 4>>> func_overlaps;
  for (auto &future : func_candidates) {
    const CID &func_cid = future.first;
    Future &result = future.second;
    func_overlaps.emplace_back();
    auto &overlaps = func_overlaps.back();
    for (auto &item : result->map_range()) {
      for (auto &candidate : item.value().list_range()) {
        size_t m = x_m.size();
        x_m.emplace_back(std::nullopt);
        s_m.emplace_back(candidate["caller_savings"].as<int>());
        f_m.emplace_back(candidate["callee_size"].as<int>());
        for (size_t i : decodeBitVector(candidate["nodes"])) {
          if (overlaps.size() <= i)
            overlaps.resize(i + 1);
          overlaps[i].push_back(m);
        }
        callees.emplace_back(evaluator.evaluateAsync(
            extracted_callees_version, func_cid, candidate["nodes"]));
      }
    }
    result.freeNode();
  }

  StringMap<size_t> callee_indices;
  std::vector<std::optional<LinearProgram::Var>> y_n;
  std::vector<int> f_n;
  std::vector<size_t> callee_for_caller;
  std::vector<int> sum_s_n;
  for (size_t m = 0; m < callees.size(); ++m) {
    callees[m].freeNode();
    auto cid = callees[m].getCID();
    size_t n;
    if (callee_indices.count(cid_key(cid))) {
      n = callee_indices[cid_key(cid)];
      f_n[n] = std::min(f_n[n], f_m[m]);
      sum_s_n[n] += s_m[m];
    } else {
      n = callee_indices[cid_key(cid)] = y_n.size();
      y_n.emplace_back(std::nullopt);
      sum_s_n.emplace_back(s_m[m]);
      f_n.emplace_back(f_m[m]);
    }
    callee_for_caller.emplace_back(n);
  }

  // Eliminate candidates that can't be profitable.
  std::vector<bool> useless_m(x_m.size());
  std::vector<bool> useless_n(y_n.size());
  for (size_t n = 0; n < y_n.size(); ++n)
    useless_n[n] = sum_s_n[n] <= f_n[n];
  for (size_t m = 0; m < x_m.size(); ++m)
    useless_m[m] = useless_n[callee_for_caller[m]];

  // Identify candidates that are always profitable.
  std::vector<bool> has_overlap(x_m.size());
  for (const auto &overlaps : func_overlaps) {
    for (const auto &o : overlaps) {
      size_t count = 0;
      for (size_t m : o)
        if (!useless_m[m])
          ++count;
      if (count >= 2)
        for (size_t m : o)
          has_overlap[m] = true;
    }
  }
  std::vector<int> sum_no_overlap_s_n(y_n.size());
  for (size_t m = 0; m < x_m.size(); ++m)
    if (!has_overlap[m])
      sum_no_overlap_s_n[callee_for_caller[m]] += s_m[m];
  std::vector<bool> always_m(x_m.size());
  std::vector<bool> always_n(y_n.size());
  for (size_t n = 0; n < y_n.size(); ++n)
    always_n[n] = sum_no_overlap_s_n[n] > f_n[n];
  for (size_t m = 0; m < x_m.size(); ++m)
    always_m[m] =
        !useless_m[m] && !has_overlap[m] && always_n[callee_for_caller[m]];

  // Create variables only for candidates that can potentially be profitable,
  // but aren't necessarily profitable.
  for (size_t n = 0; n < y_n.size(); ++n)
    if (!useless_n[n] && !always_n[n])
      y_n[n] = problem.makeBoolVar(formatv("Y{0}", n).str());
  for (size_t m = 0; m < x_m.size(); ++m)
    if (!useless_m[m] && !always_m[m])
      x_m[m] = problem.makeBoolVar(formatv("X{0}", m).str());

  // Calculate objective function.
  LinearProgram::Expr objective;
  std::int64_t free_benefit = 0;
  for (size_t m = 0; m < x_m.size(); ++m) {
    if (const auto &x = x_m[m])
      objective -= s_m[m] * *x;
    else if (always_m[m])
      free_benefit -= s_m[m];
  }
  for (size_t n = 0; n < y_n.size(); ++n) {
    if (const auto &y = y_n[n])
      objective += f_n[n] * *y;
    else if (always_n[n])
      free_benefit += f_n[n];
  }
  // We could add the free_benefit directly to objective, but some solvers will
  // ignore a constant term in the objective. By using a variable, we ensure
  // all solvers report the same objective value.
  objective += free_benefit * problem.makeBoolVar("FREE");
  problem.setObjective("COST", std::move(objective));

  // Add constraints to require a callee to be outlined if the corresponding
  // caller is.
  for (size_t m = 0; m < x_m.size(); ++m) {
    const auto &x = x_m[m];
    if (!x)
      continue;
    const auto &y = y_n[callee_for_caller[m]];
    if (!y)
      continue;
    problem.addConstraint(formatv("C{0}", m).str(), *x <= *y);
  }

  // Add constraints to prevent overlapping candidates from being outlined.
  size_t overlap_var_number = 0;
  for (size_t f = 0; f < func_overlaps.size(); ++f) {
    const auto &overlaps = func_overlaps[f];
    std::vector<size_t> last_o;
    for (size_t i = 0; i < overlaps.size(); ++i) {
      if (overlaps[i].empty())
        continue;
      std::vector<size_t> o;
      LinearProgram::Expr sum;
      for (auto m : overlaps[i]) {
        const auto &x = x_m[m];
        if (x) {
          sum += *x;
          o.emplace_back(m);
        }
      }
      if (o.size() <= 1 || o == last_o)
        continue; // constraint unnecessary or redundant
      last_o = std::move(o);
      problem.addConstraint(formatv("O{0}", overlap_var_number++).str(),
                            std::move(sum) <= 1);
    }
  }

  std::string buffer;
  llvm::raw_string_ostream os(buffer);
  problem.writeFixedMPS(os);
  return Node(utf8_string_arg, os.str());
}

namespace smout {
namespace {
// Holds all information used when deciding which candidates to outline.
struct OutliningProblem {
  struct FunctionInfo {
    CID cid;

    // All candidates that could be outlined from this function.
    std::vector<size_t> candidate_indices;

    // The subset of candidate_indices selected for outlining.
    std::vector<size_t> selected_candidate_indices;

    // The compiled size of the function, with selected_candidate_indices
    // outlined from it.
    size_t current_size;

    // How many identical copies of this function there are in the original
    // program.
    size_t num_copies;

    FunctionInfo(CID cid, size_t num_copies)
        : cid(cid), num_copies(num_copies) {}
  };
  std::vector<FunctionInfo> functions;

  struct CandidateInfo {
    size_t function_index;
    size_t callee_index;

    // The number of candidates (including this one) that were extracted from
    // the original function to calculate the "savings" value. If this is too
    // low, it means the "savings" value is outdated. If this is 0, it means we
    // haven't calculated the actual savings at all, and savings ==
    // estimated_savings.
    size_t num_candidates_used_in_savings_calculation = 0;

    // The original estimate of number of bytes saved by outlining this
    // candidate.
    int estimated_savings;

    // The actual number of bytes saved by outlining this candidate, assuming
    // all the candidates in FunctionInfo::selected_candidate_indices are
    // already outlined.
    int savings;

    Node nodes;

    // Contains indices of the other candidates that conflict with this one
    // (because they both try to outline the same node).
    SparseBitVector<> overlaps;

    // If false, this candidate cannot be used because it conflicts with an
    // already-selected candidate.
    bool no_conflict = false;
  };
  std::vector<CandidateInfo> candidates;

  struct CalleeInfo {
    CID cid;
    int size;
    SmallVector<size_t, 2> candidate_indices;
    bool selected = false;

    CalleeInfo(CID cid) : cid(cid) {}
  };
  std::vector<CalleeInfo> callees;

  Evaluator &evaluator;

  OutliningProblem(Evaluator &evaluator,
                   StringMap<unsigned> &original_function_copies,
                   const Node &grouped_callees, int min_caller_savings)
      : evaluator(evaluator) {
    organizeCandidates(original_function_copies, grouped_callees,
                       min_caller_savings);
    determineOverlaps();
    markInitialConflicts();
  }

  void organizeCandidates(StringMap<unsigned> &original_function_copies,
                          const Node &grouped_callees, int min_caller_savings) {
    StringMap<size_t> function_indices;
    StringMap<size_t> callee_indices;

    for (auto &item : grouped_callees.map_range()) {
      const Node &group = item.value();
      if (group["num_unique_callees"].as<size_t>() >=
          group["num_members"].as<size_t>())
        continue;
      // TODO: if greedy_solution is a bottleneck, we could filter out callees
      // with an insufficient number of duplicates.
      Node members = evaluator.getStore().get(group["members"].as<CID>());
      for (auto &candidate : members.list_range()) {
        if (candidate["caller_savings"].as<int>() < min_caller_savings)
          continue;

        auto function = candidate["function"].as<CID>();
        auto callee = candidate["callee"].as<CID>();

        // Candidate info.
        auto m = candidates.size();
        candidates.push_back({});
        CandidateInfo &cand_info = candidates.back();
        cand_info.estimated_savings = cand_info.savings =
            candidate["caller_savings"].as<int>() *
            original_function_copies[cid_key(function)];
        cand_info.nodes = candidate["nodes"];

        // Original function info.
        if (!function_indices.count(cid_key(function))) {
          cand_info.function_index = function_indices[cid_key(function)] =
              function_indices.size();
          functions.emplace_back(function,
                                 original_function_copies[cid_key(function)]);
        } else {
          cand_info.function_index = function_indices[cid_key(function)];
        }
        functions[cand_info.function_index].candidate_indices.push_back(m);

        // Callee info.
        auto callee_size = candidate["callee_size"].as<int>();
        CalleeInfo *callee_info;
        if (!callee_indices.count(cid_key(callee))) {
          cand_info.callee_index = callee_indices[cid_key(callee)] =
              callee_indices.size();
          callees.emplace_back(callee);
          callee_info = &callees.back();
          callee_info->size = callee_size;
        } else {
          cand_info.callee_index = callee_indices[cid_key(callee)];
          callee_info = &callees[cand_info.callee_index];
          callee_info->size = std::min(callee_info->size, callee_size);
        }
        callee_info->candidate_indices.push_back(m);
      }
    }
  }

  void determineOverlaps() {
    for (FunctionInfo &func_info : functions) {
      std::vector<SmallVector<size_t, 4>> overlaps;
      for (size_t m : func_info.candidate_indices) {
        for (size_t i : decodeBitVector(candidates[m].nodes)) {
          if (overlaps.size() <= i)
            overlaps.resize(i + 1);
          overlaps[i].push_back(m);
        }
      }
      for (size_t i = 0; i < overlaps.size(); ++i) {
        if (overlaps[i].size() <= 1)
          continue;
        if (i > 0 && overlaps[i] == overlaps[i - 1])
          continue;
        for (size_t m0 : overlaps[i])
          for (size_t m1 : overlaps[i])
            if (m0 != m1)
              candidates[m0].overlaps.set(m1);
      }
    }
  }

  void computeOriginalFunctionSizes() {
    std::vector<Future> futures;
    for (size_t i = 0; i < functions.size(); ++i)
      futures.emplace_back(
          evaluator.evaluateAsync(actual_size_version, functions[i].cid));
    for (size_t i = 0; i < functions.size(); ++i)
      functions[i].current_size = futures[i]->as<size_t>();
  }

  void markInitialConflicts() {
    // Mark initial conflicts, for callees that have multiple duplicates that
    // overlap with each other.
    for (CalleeInfo &ci2 : callees) {
      SparseBitVector<> conflicts;
      for (size_t m : ci2.candidate_indices) {
        if (conflicts.test(m))
          continue;
        candidates[m].no_conflict = true;
        conflicts |= candidates[m].overlaps;
      }
    }
  }

  void updateCallerSavings(const ArrayRef<size_t> &candidates_to_update) {
    // Actually extract the caller for the given candidates and measure
    // the resulting function size, so we know exactly what the size savings
    // are.

    // FIXME: the caller size calculation might be an overestimate because of
    // missing attributes on the callee declaration. E.g., even if the
    // original function had "nothrow", the callee declaration we use here
    // will not have "nothrow", so the extracted caller might be larger than
    // necessary.

    // FIXME: depending on which callee we choose to outline, we might
    // outline multiple candidates from the same function at once. This code
    // will be inaccurate in that case because it considers each candidate
    // separately.

    std::vector<Future> caller_futures;
    for (size_t m_to_update : candidates_to_update) {
      CandidateInfo &ci = candidates[m_to_update];
      // Check if we already have an up-to-date size calculation.
      if (!ci.no_conflict ||
          ci.num_candidates_used_in_savings_calculation ==
              functions[ci.function_index].selected_candidate_indices.size() +
                  1)
        continue;
      Node node(node_list_arg);
      for (size_t m : functions[ci.function_index].selected_candidate_indices) {
        size_t n = candidates[m].callee_index;
        std::string name =
            "outlined.callee." + callees[n].cid.asString(Multibase::base32);
        // TODO: check for conflicting names.
        node.emplace_back(
            Node(node_map_arg, {{"nodes", candidates[m].nodes},
                                {"name", Node(utf8_string_arg, name)}}));
      }
      size_t n = ci.callee_index;
      std::string name =
          "outlined.callee." + callees[n].cid.asString(Multibase::base32);
      // TODO: check for conflicting names.
      node.emplace_back(
          Node(node_map_arg,
               {{"nodes", ci.nodes}, {"name", Node(utf8_string_arg, name)}}));
      caller_futures.emplace_back(evaluator.evaluateAsync(
          extracted_caller_version, functions[ci.function_index].cid, node));
    }

    std::vector<Future> caller_size_futures;
    for (Future &future : caller_futures) {
      caller_size_futures.emplace_back(
          evaluator.evaluateAsync(actual_size_version, future.getCID()));
      future.freeNode();
    }

    size_t i = 0;
    for (size_t m_to_update : candidates_to_update) {
      CandidateInfo &ci = candidates[m_to_update];
      // Check if we already have an up-to-date size calculation.
      if (!ci.no_conflict ||
          ci.num_candidates_used_in_savings_calculation ==
              functions[ci.function_index].selected_candidate_indices.size() +
                  1)
        continue;
      auto new_size = caller_size_futures[i]->as<size_t>();
      ci.savings = static_cast<int>(functions[ci.function_index].current_size) -
                   new_size;
      ci.num_candidates_used_in_savings_calculation =
          functions[ci.function_index].selected_candidate_indices.size() + 1;
      ++i;
    }
  }

  void updateAllCallerSavings() {
    std::vector<size_t> all_candidates;
    for (size_t m = 0; m < candidates.size(); ++m)
      all_candidates.push_back(m);
    updateCallerSavings(all_candidates);
  }

  int calculateCalleeBenefit(size_t n) {
    int benefit = -callees[n].size;
    for (size_t m : callees[n].candidate_indices)
      if (candidates[m].no_conflict && candidates[m].savings > 0)
        benefit += candidates[m].savings *
                   functions[candidates[m].function_index].num_copies;
    return benefit;
  }

  void findBestCallee(size_t &best_n, int &best_benefit) {
    // Calculate the benefit of each callee, and choose the best one.
    best_n = 0;
    best_benefit = 0;
    for (size_t n = 0; n < callees.size(); ++n) {
      if (callees[n].selected)
        continue;
      int benefit = calculateCalleeBenefit(n);
      if (benefit > best_benefit) {
        best_n = n;
        best_benefit = benefit;
      }
    }
  }

  void selectCalleeForOutlining(size_t n) {
    callees[n].selected = true;
    for (size_t m : callees[n].candidate_indices) {
      CandidateInfo &cand_info = candidates[m];
      if (!cand_info.no_conflict)
        continue;
      cand_info.no_conflict = false;
      if (cand_info.savings <= 0)
        continue; // not profitable
      functions[cand_info.function_index].selected_candidate_indices.push_back(
          m);
      // FIXME: if we're adding multiple candidates from the same function at
      // once, we can get an incorrect current_size.
      functions[cand_info.function_index].current_size -= cand_info.savings;
      for (size_t m2 : cand_info.overlaps) {
        CandidateInfo &cand_info2 = candidates[m2];
        if (!cand_info2.no_conflict)
          continue;
        cand_info2.no_conflict = false;
      }
    }
  }

  void disableCallee(size_t n) {
    for (size_t m : callees[n].candidate_indices)
      candidates[m].no_conflict = false;
  }
};
} // end anonymous namespace
} // end namespace smout

NodeOrCID smout::greedy_solution(Evaluator &evaluator, Link options, Link mod) {
  Node stripped_options = *options;
  int min_benefit = stripped_options.get_value_or<int>("min_benefit", 1);
  int min_caller_savings =
      stripped_options.get_value_or<int>("min_caller_savings", 1);
  bool compile_all_callers =
      stripped_options.get_value_or<bool>("compile_all_callers", false);
  bool verify_caller_savings =
      stripped_options.get_value_or<bool>("verify_caller_savings", false);
  bool use_alive2 = stripped_options.get_value_or<bool>("use_alive2", false);

  // Remove smout.greedy_solution options so we don't pass them to
  // smout.grouped_callees (which doesn't understand them anyway).
  stripped_options.erase("min_benefit");
  stripped_options.erase("min_caller_savings");
  stripped_options.erase("compile_all_callers");
  stripped_options.erase("verify_caller_savings");
  stripped_options.erase("use_alive2");

  StringMap<unsigned> original_function_copies;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    original_function_copies[cid_key(func_cid)]++;
  }

  Link grouped_callees = evaluator.evaluate(
      use_alive2 ? grouped_refinements_version : grouped_callees_version,
      stripped_options, mod);

  OutliningProblem problem(evaluator, original_function_copies,
                           *grouped_callees, min_caller_savings);

  if (compile_all_callers || verify_caller_savings)
    problem.computeOriginalFunctionSizes();

  // Determine which callees to use.
  unsigned total_benefit = 0;
  while (true) {
    if (compile_all_callers)
      problem.updateAllCallerSavings();

    // Calculate the benefit of each callee, and choose the best one.
    size_t best_n;
    int best_benefit;
    problem.findBestCallee(best_n, best_benefit);

    if (best_benefit < min_benefit)
      break;

    if (!compile_all_callers && verify_caller_savings) {
      // We need to verify that outlining this callee is actually profitable.
      problem.updateCallerSavings(problem.callees[best_n].candidate_indices);
      best_benefit = problem.calculateCalleeBenefit(best_n);
      if (best_benefit <= 0) {
        problem.disableCallee(best_n);
        continue;
      }
    }

    total_benefit += best_benefit;
    problem.selectCalleeForOutlining(best_n);
  }

  Node result_functions(node_map_arg);
  for (auto &fi : problem.functions) {
    if (fi.selected_candidate_indices.empty())
      continue;
    Node cands_node(node_list_arg);
    for (size_t m : fi.selected_candidate_indices) {
      auto &cand_info = problem.candidates[m];
      auto &callee_info = problem.callees[cand_info.callee_index];
      const Node &nodes = cand_info.nodes;
      cands_node.emplace_back(
          Node(node_map_arg,
               {{"nodes", nodes},
                {"callee", Node(evaluator.getStore(), callee_info.cid)},
                {"caller_savings", cand_info.savings},
                {"estimated_caller_savings", cand_info.estimated_savings},
                {"callee_size", callee_info.size}}));
    }
    const CID &cid = fi.cid;
    auto key = cid.asString(Multibase::base64url);
    result_functions[key] =
        Node(node_map_arg, {{"function", Node(evaluator.getStore(), cid)},
                            {"candidates", cands_node}});
  }
  return Node(node_map_arg, {
                                {"functions", result_functions},
                                {"total_benefit", total_benefit},
                            });
}

NodeOrCID smout::extracted_caller(Evaluator &evaluator, Link func,
                                  Link callees) {
  ExitOnError Err("smout.extracted_caller: ");
  Context context;
  auto m = Err(parseBitcodeFile(
      MemoryBufferRef(func->as<StringRef>(byte_string_arg), ""), context));
  Function &f = getSoleDefinition(*m);

  std::vector<SparseBitVector<>> bvs;
  for (const auto &callee : callees->list_range())
    bvs.emplace_back(decodeBitVector(callee["nodes"]));

  FunctionAnalysisManager am = makeFAM(Node(node_map_arg));
  auto &deps = am.getResult<OutliningDependenceAnalysis>(f);

  OutliningCallerExtractor extractor(f, deps, bvs);
  extractor.modifyDefinition();
  for (size_t i = 0; i < callees->size(); ++i) {
    Function *callee = extractor.callees[i].createDeclaration();
    StringRef name = (*callees)[i]["name"].as<StringRef>();
    callee->setName(name);
    if (callee->getName() != name) {
      // We may be extracting multiple copies of the same callee.
      callee->replaceAllUsesWith(callee->getParent()->getFunction(name));
    }
  }

  // TODO: call postprocessModule(*m). This will require extra handling in
  // optimized() because it can change the function attributes, and the
  // function attributes on the caller need to match the attributes in the
  // remainder module.
  bcdb::Splitter splitter(*m);
  auto mpart = splitter.SplitGlobal(&f);
  SmallVector<char, 0> buffer;
  bcdb::WriteAlignedModule(*mpart, buffer);
  return Node(byte_string_arg, buffer);
}

NodeOrCID smout::outlined_module(Evaluator &evaluator, Link mod,
                                 Link solution) {
  Node mod_node = *mod;
  ExitOnError err("smout.optimized: ");
  Context context;
  Node old_remainder = evaluator.getStore().get((*mod)["remainder"].as<CID>());
  auto remainder = err(parseBitcodeFile(
      MemoryBufferRef(old_remainder.as<StringRef>(byte_string_arg), ""),
      context));
  IRMover mover(*remainder);

  // Choose name for each callee, insert callee into module, and start
  // extracting callers.
  StringMap<std::string> callee_names;
  StringMap<Future> callers;
  for (const auto &function_item : (*solution)["functions"].map_range()) {
    const auto &function = function_item.value();
    Node callees(node_list_arg);
    for (const auto &candidate : function["candidates"].list_range()) {
      auto cid = candidate["callee"].as<CID>();
      if (!callee_names.count(cid_key(cid))) {
        // NOTE: we could base the callee name on the first caller name we come
        // across, but that would make caching less effective.
        std::string name = "outlined.callee." + cid.asString(Multibase::base32);
        while (mod_node["functions"].count(name))
          name += "_";
        callee_names[cid_key(cid)] = name;

        mod_node["functions"][name] = Node(evaluator.getStore(), cid);

        // Copy callee declaration into remainder module.
        Node callee_bc = evaluator.getStore().get(cid);
        auto callee_m = err(parseBitcodeFile(
            MemoryBufferRef(callee_bc.as<StringRef>(byte_string_arg), ""),
            context));
        Function &callee_f = getSoleDefinition(*callee_m);
        callee_f.deleteBody();
        new UnreachableInst(context,
                            BasicBlock::Create(context, "", &callee_f));
        callee_f.setName(name);
        assert(callee_f.getName() == name && "name conflict");
        callee_f.setDSOLocal(true);
        callee_f.setLinkage(GlobalValue::InternalLinkage);
        err(mover.move(
            std::move(callee_m), {&callee_f},
            [](GlobalValue &, IRMover::ValueAdder) {},
            /*IsPerformingImport*/ false));
      }
      callees.emplace_back(
          Node(node_map_arg,
               {{"name", Node(utf8_string_arg, callee_names[cid_key(cid)])},
                {"nodes", candidate["nodes"]}}));
    }
    auto cid = function["function"].as<CID>();
    callers.insert_or_assign(cid_key(cid),
                             evaluator.evaluateAsync(extracted_caller_version,
                                                     cid, std::move(callees)));
  }

  // Insert callers into module.
  for (auto &item : mod_node["functions"].map_range()) {
    auto orig = item.value().as<CID>();
    auto caller_iter = callers.find(cid_key(orig));
    if (caller_iter != callers.end())
      item.value() =
          Node(evaluator.getStore(), caller_iter->getValue().getCID());
  }

  // Replace remainder module.
  SmallVector<char, 0> buffer;
  bcdb::WriteAlignedModule(*remainder, buffer);
  mod_node["remainder"] =
      Node(evaluator.getStore(),
           evaluator.getStore().put(Node(byte_string_arg, buffer)));

  return mod_node;
}

NodeOrCID smout::optimized(Evaluator &evaluator, Link options, Link mod) {
  Link solution = evaluator.evaluate(greedy_solution_version, options, mod);
  return evaluator.evaluate(outlined_module_version, mod, solution).getCID();
}

NodeOrCID smout::refinements_for_set(Evaluator &evaluator, Link options,
                                     Link members) {
  // Precondition: members is not empty.
  // Precondition: members is sorted by CID.
  // Precondition: members contains no duplicates.
  // Postcondition: if members[0] is in the result at all, it is the first
  // member of the first sublist.

  if (members->size() <= 1)
    return Node(node_list_arg);
  CID first_cid = (*members)[0].as<CID>();
  Node options_nopoison_noundef = *options;
  options_nopoison_noundef["disable_poison_input"] = true;
  options_nopoison_noundef["disable_undef_input"] = true;
  std::size_t num_unhelpful_results = 0;
  bool successfully_solved = false;
  Node tv_result(node_map_arg);

  // Indices of members that have been proven equivalent to first_cid.
  std::vector<std::size_t> equivalent_to_first;

  // Run alive.tv between the first member and each other member, in order.
  for (std::size_t i = 1; i < members->size(); ++i) {
    // Validate forward and backward ignoring poison and undef (unsound but
    // fast), then forward and backward including poison and undef (sound but
    // slow).
    //
    // TODO: is it worth taking advantage of refinements that are valid in one
    // direction but not the other? For now, we ignore them.
    for (unsigned j = 0; j < 4; ++j) {
      CID src = first_cid;
      CID tgt = (*members)[i].as<CID>();
      if (j & 1) // forward or backward?
        std::swap(src, tgt);
      tv_result = *evaluator.evaluate(
          "alive.tv_v2", j & 2 ? *options : options_nopoison_noundef, src, tgt);
      if (tv_result.at("status") == "syntactic_eq") {
        // No need to run the other tests.
        break;
      } else if (tv_result.at_or_null("valid") != true) {
        // Unsound or unknown result.
        break;
      }
    }

    if (tv_result.at_or_null("valid") == true) {
      if (tv_result.at("status") != "syntactic_eq") {
        // We successfully used the solver to prove this refinement correct, so
        // we know Alive2 is capable of handling first_cid at least some of the
        // time.
        successfully_solved = true;
      }
      equivalent_to_first.push_back(i);
    } else if (tv_result.count("test_input")) {
      break;
    } else {
      // Failure without producing a test input. Possible reasons:
      // - unsupported by Alive2
      // - solver timeout
      // - loops
      // - precondition is always false
      num_unhelpful_results++;
      if (!successfully_solved && num_unhelpful_results >= 10) {
        // We're repeatedly failing to validate; maybe Alive2 just can't handle
        // first_cid. Let's give up on it.
        break;
      }
    }
  }

  // At this point, we've stopped applying alive.tv, for one of these reasons:
  // - We tested first_cid against every other item in members.
  // - We're giving up on first_cid because Alive2 didn't work well on it.
  // - We found a test input that makes first_cid and some other item give
  //   different results. We can use this test input to divide all the members
  //   into clusters.

  std::vector<Node> clusters;
  if (tv_result.count("test_input")) {
    // Evaluate all members on the test input.
    CID test_input = tv_result["test_input"].as<CID>();
    std::vector<Future> futures;
    auto iter = equivalent_to_first.begin();
    for (std::size_t i = 0; i < members->size(); ++i) {
      if (iter != equivalent_to_first.end() && i == *iter) {
        // Skip items we already know are equivalent to first_cid.
        iter++;
        continue;
      }
      futures.emplace_back(
          evaluator.evaluateAsync("alive.interpret", options.getCID(),
                                  (*members)[i].as<CID>(), test_input));
    }

    StringMap<std::size_t> cluster_indices;
    iter = equivalent_to_first.begin();
    auto futures_iter = futures.begin();
    for (std::size_t i = 0; i < members->size(); ++i) {
      if (iter != equivalent_to_first.end() && i == *iter) {
        iter++;
        continue;
      }
      Future &future = *futures_iter++;
      if ((*future)["status"] == "unsupported") {
        // If the interpreter doesn't support this function (at least on this
        // input) there's probably nothing useful we can do with it.
        continue;
      }
      auto key = cid_key(future.getCID());
      if (!cluster_indices.count(key)) {
        cluster_indices[key] = clusters.size();
        clusters.emplace_back(node_list_arg);
      }
      clusters[cluster_indices[key]].push_back((*members)[i]);
    }
  } else {
    // We don't have a test input. Just add everything to one cluster.
    clusters.emplace_back(node_list_arg);
    auto iter = equivalent_to_first.begin();
    for (std::size_t i = 0; i < members->size(); ++i) {
      if (iter != equivalent_to_first.end() && i == *iter) {
        // Skip items we already know are equivalent to first_cid.
        iter++;
        continue;
      }
      clusters[0].push_back((*members)[i]);
    }
  }

  if (clusters.size() == 1) {
    // We couldn't generate any clusters, either because we don't have a test
    // input or because alive.interpret gave the same result for everything
    // (due to not matching alive.tv's behavior). Either way, we should remove
    // first_cid from the set to prevent infinite recursion.
    auto range = clusters[0].list_range();
    if (*range.begin() == Node(evaluator.getStore(), first_cid))
      clusters[0] = Node(node_list_arg, range.begin() + 1, range.end());
  }

  // Recursive checking on each cluster.
  std::vector<Future> futures;
  for (const Node &cluster : clusters)
    futures.emplace_back(evaluator.evaluateAsync(refinements_for_set_version,
                                                 options.getCID(), cluster));

  // Organize results. We mostly just want to concatenate all the lists
  // returned from the futures, but we also need to add the items in
  // equivalent_to_first at the appropriate place.
  Node result(node_list_arg);
  bool first = true;
  for (Future &future : futures) {
    Node node = *future;
    if (first && !equivalent_to_first.empty()) {
      first = false;
      // Put functions equivalent to first_cid in result[0].
      result.emplace_back(node_list_arg);
      result[0].emplace_back(evaluator.getStore(), first_cid);
      for (std::size_t i : equivalent_to_first)
        result[0].emplace_back((*members)[i]);
      if (!node.empty() && node[0][0].as<CID>() == first_cid) {
        // first_cid was returned in one of the results. We need to add
        // everything else in that group to result[0], then handle the rest of
        // the groups normally.
        for (std::size_t i = 1; i < node[0].size(); ++i)
          result[0].emplace_back(node[0][i]);
        for (std::size_t i = 1; i < node.size(); ++i)
          result.emplace_back(std::move(node[i]));
        continue;
      }
      // first_cid was not returned in one of the results. Handle the rest of
      // the groups normally.
    }
    for (const Node &sub : node.list_range())
      result.emplace_back(sub);
  }
  return result;
}

NodeOrCID smout::refinements_for_group(Evaluator &evaluator, Link options,
                                       Link members) {
  Node result = *members;
  members.freeNode();

  // Find all unique callees and their sizes.
  StringMap<unsigned> callee_sizes;
  StringSet unique_set;
  std::vector<CID> unique;
  for (const auto &item : result.list_range()) {
    const CID &cid = item["callee"].as<CID>();
    if (unique_set.insert(cid_key(cid)).second) {
      unique.push_back(cid);
      callee_sizes[cid_key(cid)] = item["callee_size"].as<unsigned>();
    }
  }
  std::sort(unique.begin(), unique.end());

  // Search for valid refinements between callees.
  Node set_node(node_list_arg);
  for (const CID &cid : unique)
    set_node.emplace_back(evaluator.getStore(), cid);
  auto sets =
      evaluator.evaluate(refinements_for_set_version, options, set_node);

  // Determine the best callee in each group.
  StringMap<CID> refined_callee;
  for (const auto &set : sets->list_range()) {
    unsigned best_size = 0xffffffff;
    CID best_cid = set[0].as<CID>();
    for (const auto &item : set.list_range()) {
      CID cid = item.as<CID>();
      if (callee_sizes[cid_key(cid)] < best_size) {
        best_size = callee_sizes[cid_key(cid)];
        best_cid = cid;
      }
    }
    for (const auto &item : set.list_range())
      if (item.as<CID>() != best_cid)
        refined_callee.try_emplace(cid_key(item.as<CID>()), best_cid);
  }

  // Replace each callee with the best refined callee.
  for (auto &item : result.list_range()) {
    CID orig = item["callee"].as<CID>();
    if (!refined_callee.count(cid_key(orig)))
      continue;
    const CID &refined = refined_callee.find(cid_key(orig))->getValue();
    item.erase("estimated_callee_size");
    item["callee"] = Node(evaluator.getStore(), refined);
    item["callee_size"] = callee_sizes[cid_key(refined)];
  }

  return result;
}

NodeOrCID smout::grouped_refinements(Evaluator &evaluator, Link options,
                                     Link mod) {
  Node grouped_callees =
      *evaluator.evaluate(grouped_callees_version, options, mod);
  std::vector<Future> futures;
  for (const auto &item : grouped_callees.map_range())
    futures.emplace_back(
        evaluator.evaluateAsync(refinements_for_group_version, options,
                                item.value()["members"].as<CID>()));
  size_t i = 0;
  for (auto &item : grouped_callees.map_range()) {
    item.value()["members"] = Node(evaluator.getStore(), futures[i].getCID());
    futures[i].freeNode();
    i++;
  }
  return grouped_callees;
}

void smout::registerFuncs(Evaluator &evaluator) {
  evaluator.registerFunc(actual_size_version, &actual_size);
  evaluator.registerFunc(candidates_version, &candidates);
  evaluator.registerFunc(grouped_candidates_version, &grouped_candidates);
  evaluator.registerFunc(extracted_callees_version, &extracted_callees);
  evaluator.registerFunc(grouped_callees_for_function_version,
                         &grouped_callees_for_function);
  evaluator.registerFunc(grouped_callees_version, &grouped_callees);
  evaluator.registerFunc(ilp_problem_version, &ilp_problem);
  evaluator.registerFunc(greedy_solution_version, &greedy_solution);
  evaluator.registerFunc(extracted_caller_version, &extracted_caller);
  evaluator.registerFunc(outlined_module_version, &outlined_module);
  evaluator.registerFunc(optimized_version, &optimized);
  evaluator.registerFunc(refinements_for_set_version, &refinements_for_set);
  evaluator.registerFunc(refinements_for_group_version, &refinements_for_group);
  evaluator.registerFunc(grouped_refinements_version, &grouped_refinements);
}
