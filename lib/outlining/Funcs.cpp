#include "outlining/Funcs.h"

#include <cstdint>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <vector>

#include "bcdb/AlignBitcode.h"
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

const char *smout::candidates_version = "smout.candidates_v1";
const char *smout::grouped_candidates_version = "smout.grouped_candidates_v1";
const char *smout::extracted_callees_version = "smout.extracted_callees_v2";
const char *smout::grouped_callees_for_function_version =
    "smout.grouped_callees_for_function_v2";
const char *smout::grouped_callees_version = "smout.grouped_callees_v2";
const char *smout::ilp_problem_version = "smout.ilp_problem";
const char *smout::greedy_solution_version = "smout.greedy_solution_v2";
const char *smout::extracted_caller_version = "smout.extracted_caller_v2";
const char *smout::optimized_version = "smout.optimized_v2";
const char *smout::refinements_for_group_version =
    "smout.refinements_for_group_v0";
const char *smout::grouped_refinements_version = "smout.grouped_refinements_v2";

static FunctionAnalysisManager makeFAM(const Node &options) {
  OutliningCandidatesOptions cand_opts;
  cand_opts.max_adjacent =
      options.get_value_or<size_t>("max_adjacent", cand_opts.max_adjacent);
  cand_opts.max_args =
      options.get_value_or<size_t>("max_args", cand_opts.max_args);
  cand_opts.max_nodes =
      options.get_value_or<size_t>("max_nodes", cand_opts.max_nodes);

  FunctionAnalysisManager am;
  // TODO: Would it be faster to just register the analyses we need?
  PassBuilder().registerFunctionAnalyses(am);
  am.registerPass([] { return FalseMemorySSAAnalysis(); });
  am.registerPass([=] { return OutliningCandidatesAnalysis(cand_opts); });
  am.registerPass([] { return OutliningDependenceAnalysis(); });
  am.registerPass([] { return SizeModelAnalysis(); });
  return am;
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

  std::vector<std::uint8_t> bytes;
  node.save_cbor(bytes);
  return Multibase::base64pad.encodeWithoutPrefix(bytes);
}

static bool isGroupWorthExtracting(NodeRef &options, const Node &group) {
  size_t min_callee_size = group["min_callee_size"].as<size_t>();
  size_t total_caller_savings = group["total_caller_savings"].as<size_t>();
  return total_caller_savings > min_callee_size;
}

NodeOrCID smout::candidates(Evaluator &evaluator, NodeRef options,
                            NodeRef func) {
  ExitOnError Err("smout.candidates: ");
  LLVMContext context;
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

NodeOrCID smout::grouped_candidates(Evaluator &evaluator, NodeRef options,
                                    NodeRef mod) {
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync(candidates_version, options, func_cid));
  }
  struct Group {
    size_t total_caller_savings = 0;
    size_t min_callee_size = 1000000000;
    Node members = Node(node_list_arg);
  };
  StringMap<Group> groups;
  for (auto &func_item : func_candidates) {
    const CID &func_cid = func_item.first;
    Future &result = func_item.second;
    for (auto &item : result->map_range()) {
      Group &group = groups[item.key()];
      for (auto &candidate : item.value().list_range()) {
        group.total_caller_savings += candidate["caller_savings"].as<size_t>();
        group.min_callee_size = std::min(group.min_callee_size,
                                         candidate["callee_size"].as<size_t>());
        Node candidate_changed = candidate;
        candidate_changed["function"] = Node(func_cid);
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
              {"members", Node(evaluator.getStore().put(group.members))}});
  }
  return groups_node;
}

NodeOrCID smout::extracted_callees(Evaluator &evaluator, NodeRef func,
                                   NodeRef node_sets) {
  ExitOnError Err("smout.extracted_callees: ");
  LLVMContext context;
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

  bcdb::Splitter splitter(*m);
  Node result(node_list_arg);
  for (Function *callee : callees) {
    auto size = SizeModelResults(*callee).this_function_total_size;
    auto mpart = splitter.SplitGlobal(callee);
    SmallVector<char, 0> buffer;
    bcdb::WriteAlignedModule(*mpart, buffer);
    result.emplace_back(
        Node(node_map_arg, {{"callee", Node(evaluator.getStore().put(
                                           Node(byte_string_arg, buffer)))},
                            {"size", size}}));
  }
  assert(node_sets->size() == result.size());
  return result;
}

NodeOrCID smout::grouped_callees_for_function(Evaluator &evaluator,
                                              NodeRef options,
                                              NodeRef grouped_candidates,
                                              NodeRef func) {
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
      node["caller_savings"] = node["caller_savings"].as<int>() + delta;

      group.emplace_back(node);
    }
  }
  return result;
}

NodeOrCID smout::grouped_callees(Evaluator &evaluator, NodeRef options,
                                 NodeRef mod) {
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
        candidate_changed["function"] = Node(func_cid);
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
    group["members"] = Node(evaluator.getStore().put(item.getValue()));
    group["num_unique_callees"] = group_unique_callees[item.getKey()];
    group["num_callees_without_duplicates"] =
        group_callees_without_duplicates[item.getKey()];
    result[item.getKey()] = group;
  }
  return result;
}

NodeOrCID smout::ilp_problem(Evaluator &evaluator, NodeRef options,
                             NodeRef mod) {
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

NodeOrCID smout::greedy_solution(Evaluator &evaluator, NodeRef options,
                                 NodeRef mod) {
  Node stripped_options = *options;
  int min_benefit = stripped_options.get_value_or<int>("min_benefit", 1);
  stripped_options.erase("min_benefit");
  int min_caller_savings =
      stripped_options.get_value_or<int>("min_caller_savings", 1);
  stripped_options.erase("min_caller_savings");

  StringMap<unsigned> original_function_copies;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    original_function_copies[cid_key(func_cid)]++;
  }
  auto grouped_callees =
      evaluator.evaluate(grouped_callees_version, stripped_options, mod);

  StringMap<size_t> function_indices;
  StringMap<size_t> callee_indices;

  struct FunctionInfo {
    CID cid;
    std::vector<size_t> candidate_indices;

    FunctionInfo(CID cid) : cid(cid) {}
  };
  std::vector<FunctionInfo> functions;

  struct CandidateInfo {
    int savings;
    size_t function_index;
    Node nodes;
    size_t callee_index;
    SparseBitVector<> overlaps;
    bool no_conflict = false;
  };
  std::vector<CandidateInfo> candidates;

  struct CalleeInfo {
    CID cid;
    int size;
    SmallVector<size_t, 2> candidate_indices;
    int benefit;

    CalleeInfo(CID cid) : cid(cid) {}
  };
  std::vector<CalleeInfo> callees;

  // Organize candidates. TODO: factor out code shared with ilp_problem.
  for (auto &item : grouped_callees->map_range()) {
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
      cand_info.savings = candidate["caller_savings"].as<int>() *
                          original_function_copies[cid_key(function)];
      cand_info.nodes = candidate["nodes"];

      // Original function info.
      if (!function_indices.count(cid_key(function))) {
        cand_info.function_index = function_indices[cid_key(function)] =
            function_indices.size();
        functions.emplace_back(function);
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

  // Determine overlaps.
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

  // Calculate benefit of using each callee. If a callee has multiple
  // duplicates that overlap with each other, only consider the first one seen.
  for (CalleeInfo &ci2 : callees) {
    ci2.benefit = -ci2.size;
    SparseBitVector<> conflicts;
    for (size_t m : ci2.candidate_indices) {
      if (conflicts.test(m))
        continue;
      candidates[m].no_conflict = true;
      conflicts |= candidates[m].overlaps;
      ci2.benefit += candidates[m].savings;
    }
  }

  // Determine which callees to use.
  unsigned total_benefit = 0;
  std::vector<size_t> selected_m;
  std::vector<size_t> selected_n;
  while (true) {
    size_t best_n = 0;
    int best_benefit = 0;
    for (size_t n = 0; n < callees.size(); ++n) {
      if (callees[n].benefit > best_benefit) {
        best_n = n;
        best_benefit = callees[n].benefit;
      }
    }
    if (best_benefit < min_benefit)
      break;

    size_t n = best_n;
    selected_n.push_back(n);
    total_benefit += best_benefit;
    callees[n].benefit = -1; // don't use this n again
    for (size_t m : callees[n].candidate_indices) {
      CandidateInfo &cand_info = candidates[m];
      if (!cand_info.no_conflict)
        continue;
      selected_m.push_back(m);
      cand_info.no_conflict = false;
      for (size_t m2 : cand_info.overlaps) {
        CandidateInfo &cand_info2 = candidates[m2];
        if (!cand_info2.no_conflict)
          continue;
        callees[cand_info2.callee_index].benefit -= cand_info2.savings;
        cand_info2.no_conflict = false;
      }
    }
  }

  Node result_functions(node_map_arg);
  for (size_t m : selected_m) {
    CandidateInfo &cand_info = candidates[m];
    CalleeInfo &callee_info = callees[cand_info.callee_index];
    const CID &cid = functions[cand_info.function_index].cid;
    const Node &nodes = cand_info.nodes;
    auto key = cid.asString(Multibase::base64url);
    auto &value = result_functions[key];
    if (value == Node())
      value = Node(node_map_arg, {{"function", Node(cid)},
                                  {"candidates", Node(node_list_arg)}});
    value["candidates"].emplace_back(
        Node(node_map_arg, {{"nodes", nodes},
                            {"callee", Node(callee_info.cid)},
                            {"caller_savings", cand_info.savings},
                            {"callee_size", callee_info.size}}));
  }
  return Node(node_map_arg, {
                                {"functions", result_functions},
                                {"total_benefit", total_benefit},
                            });
}

NodeOrCID smout::extracted_caller(Evaluator &evaluator, NodeRef func,
                                  NodeRef callees) {
  ExitOnError Err("smout.extracted.caller: ");
  LLVMContext context;
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

  bcdb::Splitter splitter(*m);
  auto mpart = splitter.SplitGlobal(&f);
  SmallVector<char, 0> buffer;
  bcdb::WriteAlignedModule(*mpart, buffer);
  return Node(byte_string_arg, buffer);
}

NodeOrCID smout::optimized(Evaluator &evaluator, NodeRef options, NodeRef mod) {
  NodeRef solution = evaluator.evaluate(greedy_solution_version, options, mod);
  Node mod_node = *mod;

  ExitOnError err("smout.optimized: ");
  LLVMContext context;
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

        mod_node["functions"][name] = Node(cid);

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
      item.value() = Node(caller_iter->getValue().getCID());
  }

  // Replace remainder module.
  SmallVector<char, 0> buffer;
  bcdb::WriteAlignedModule(*remainder, buffer);
  mod_node["remainder"] =
      Node(evaluator.getStore().put(Node(byte_string_arg, buffer)));

  return mod_node;
}

NodeOrCID smout::refinements_for_group(Evaluator &evaluator, NodeRef options,
                                       NodeRef members) {
  StringSet unique_set;
  std::vector<CID> unique;
  for (const auto &item : members->list_range())
    if (unique_set.insert(cid_key(item["callee"].as<CID>())).second)
      unique.push_back(item["callee"].as<CID>());
  members.freeNode();
  unique_set.clear();
  std::vector<Future> pairs;
  for (size_t i = 0; i < unique.size(); ++i) {
    for (size_t j = 0; j < unique.size(); ++j) {
      if (i == j)
        continue;
      pairs.emplace_back(evaluator.evaluateAsync("alive.tv", Node(node_map_arg),
                                                 unique[i], unique[j]));
    }
  }
  size_t k = 0;
  Node result(node_list_arg);
  for (size_t i = 0; i < unique.size(); ++i) {
    for (size_t j = 0; j < unique.size(); ++j) {
      if (i == j)
        continue;
      if ((*pairs[k])["valid"] == true)
        result.emplace_back(Node(node_map_arg, {
                                                   {"src", Node(unique[i])},
                                                   {"tgt", Node(unique[j])},
                                               }));
      k++;
    }
  }
  return result;
}

NodeOrCID smout::grouped_refinements(Evaluator &evaluator, NodeRef options,
                                     NodeRef mod) {
  Node grouped_callees =
      *evaluator.evaluate(grouped_callees_version, options, mod);
  std::vector<Future> futures;
  for (const auto &item : grouped_callees.map_range())
    futures.emplace_back(
        evaluator.evaluateAsync(refinements_for_group_version, options,
                                item.value()["members"].as<CID>()));
  size_t i = 0;
  for (auto &item : grouped_callees.map_range()) {
    item.value()["refinements"] = Node(futures[i].getCID());
    item.value()["num_valid_refinements"] = futures[i]->size();
    futures[i].freeNode();
    i++;
  }
  return grouped_callees;
}

void smout::registerFuncs(Evaluator &evaluator) {
  evaluator.registerFunc(candidates_version, &candidates);
  evaluator.registerFunc(grouped_candidates_version, &grouped_candidates);
  evaluator.registerFunc(extracted_callees_version, &extracted_callees);
  evaluator.registerFunc(grouped_callees_for_function_version,
                         &grouped_callees_for_function);
  evaluator.registerFunc(grouped_callees_version, &grouped_callees);
  evaluator.registerFunc(ilp_problem_version, &ilp_problem);
  evaluator.registerFunc(greedy_solution_version, &greedy_solution);
  evaluator.registerFunc(extracted_caller_version, &extracted_caller);
  evaluator.registerFunc(optimized_version, &optimized);
  evaluator.registerFunc(refinements_for_group_version, &refinements_for_group);
  evaluator.registerFunc(grouped_refinements_version, &grouped_refinements);
}
