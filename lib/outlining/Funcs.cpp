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
using bcdb::OutliningDependenceAnalysis;
using bcdb::SizeModelAnalysis;

const char *smout::candidates_version = "smout.candidates_v0";
const char *smout::grouped_candidates_version = "smout.grouped_candidates_v0";
const char *smout::extracted_callees_version = "smout.extracted_callees_v0";
const char *smout::grouped_callees_for_function_version =
    "smout.grouped_callees_for_function_v0";
const char *smout::grouped_callees_version = "smout.grouped_callees_v0";
const char *smout::ilp_problem_version = "smout.ilp_problem";
const char *smout::greedy_solution_version = "smout.greedy_solution";
const char *smout::extracted_caller_version = "smout.extracted_caller_v0";
const char *smout::optimized_version = "smout.optimized";
const char *smout::equivalent_pairs_in_group_version =
    "smout.equivalent_pairs_in_group";
const char *smout::equivalent_pairs_version = "smout.equivalent_pairs";

static FunctionAnalysisManager makeFAM() {
  FunctionAnalysisManager am;
  // TODO: Would it be faster to just register the analyses we need?
  PassBuilder().registerFunctionAnalyses(am);
  am.registerPass([] { return FalseMemorySSAAnalysis(); });
  am.registerPass([] { return OutliningCandidatesAnalysis(); });
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

  FunctionAnalysisManager am = makeFAM();
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
  FunctionAnalysisManager am = makeFAM();
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
    auto mpart = splitter.SplitGlobal(callee);
    SmallVector<char, 0> buffer;
    bcdb::WriteAlignedModule(*mpart, buffer);
    result.emplace_back(
        Node(evaluator.getStore().put(Node(byte_string_arg, buffer))));
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
      candidate_group.emplace_back(group_key);
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
      node["callee"] = (*callees)[j];
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
    if (original_cids.insert(cid_key(func_cid)).second) {
      futures.emplace_back(
          func_cid,
          evaluator.evaluateAsync(grouped_callees_for_function_version, options,
                                  grouped_candidates, func_cid));
      // TODO: For now, we can only evaluate one call at a time; otherwise, we
      // might deadlock with all threads waiting on jobs and no new threads
      // available.
      futures.back().second.wait();
    }
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
  report_fatal_error("This part of BCDB is broken and needs to be updated!");
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync(candidates_version, options, func_cid));
  }

  // Get candidates.
  std::vector<Future> callees;
  std::vector<int> s_m, f_m;
  std::vector<SparseBitVector<>> o_m;
  std::vector<size_t> func_index_m;
  std::vector<Node> nodes_m;
  for (size_t func_index = 0; func_index < func_candidates.size();
       ++func_index) {
    auto &future = func_candidates[func_index];
    const CID &func_cid = future.first;
    Future &result = future.second;
    std::vector<SmallVector<size_t, 4>> func_overlaps;
    for (auto &item : result->map_range()) {
      for (auto &candidate : item.value().list_range()) {
        size_t m = s_m.size();
        s_m.emplace_back(candidate["caller_savings"].as<int>());
        f_m.emplace_back(candidate["callee_size"].as<int>());
        o_m.emplace_back();
        func_index_m.emplace_back(func_index);
        nodes_m.emplace_back(candidate["nodes"]);
        for (size_t i : decodeBitVector(nodes_m[m])) {
          if (func_overlaps.size() <= i)
            func_overlaps.resize(i + 1);
          func_overlaps[i].push_back(m);
        }
        callees.emplace_back(evaluator.evaluateAsync(extracted_callees_version,
                                                     func_cid, nodes_m[m]));
      }
    }
    for (size_t i = 0; i < func_overlaps.size(); ++i) {
      if (i > 0 && func_overlaps[i] == func_overlaps[i - 1])
        continue;
      for (size_t m0 : func_overlaps[i])
        for (size_t m1 : func_overlaps[i])
          if (m0 != m1)
            o_m[m0].set(m1);
    }
    result.freeNode();
  }

  // Get extracted callees.
  StringMap<size_t> callee_indices;
  std::vector<int> f_n;
  std::vector<size_t> n_from_m;
  std::vector<SmallVector<size_t, 2>> m_from_n;
  for (size_t m = 0; m < callees.size(); ++m) {
    callees[m].freeNode();
    auto cid = callees[m].getCID();
    size_t n;
    if (!callee_indices.count(cid_key(cid))) {
      n = callee_indices[cid_key(cid)] = callee_indices.size();
      f_n.emplace_back(f_m[m]);
      m_from_n.emplace_back();
    } else {
      n = callee_indices[cid_key(cid)];
      f_n[n] = std::min(f_n[n], f_m[m]);
    }
    m_from_n[n].emplace_back(m);
    n_from_m.emplace_back(n);
  }

  // Calculate benefit of using each callee.
  std::vector<bool> no_conflict_m(s_m.size());
  std::vector<int> benefit_n(f_n.size());
  for (size_t n = 0; n < f_n.size(); ++n) {
    SparseBitVector<> conf;
    benefit_n[n] = -f_n[n];
    for (size_t m : m_from_n[n]) {
      if (conf.test(m))
        continue;
      no_conflict_m[m] = true;
      conf |= o_m[m];
      benefit_n[n] += s_m[m];
    }
  }

  // Determine which callees to use.
  unsigned total_benefit = 0;
  std::vector<size_t> selected_m;
  std::vector<size_t> selected_n;
  while (true) {
    size_t best_n = 0;
    int best_benefit = 0;
    for (size_t n = 0; n < f_n.size(); ++n) {
      if (benefit_n[n] > best_benefit) {
        best_n = n;
        best_benefit = benefit_n[n];
      }
    }
    if (best_benefit <= 0)
      break;

    size_t n = best_n;
    selected_n.push_back(n);
    total_benefit += best_benefit;
    benefit_n[n] = -1; // don't use this n again
    for (size_t m : m_from_n[n]) {
      if (!no_conflict_m[m])
        continue;
      selected_m.push_back(m);
      no_conflict_m[m] = false;
      for (size_t m2 : o_m[m]) {
        if (!no_conflict_m[m2])
          continue;
        benefit_n[n_from_m[m2]] -= s_m[m2];
        no_conflict_m[m2] = false;
      }
    }
  }

  Node result_functions(node_map_arg);
  for (size_t m : selected_m) {
    const CID &cid = func_candidates[func_index_m[m]].first;
    const Node &nodes = nodes_m[m];
    auto key = cid.asString(Multibase::base64url);
    auto &value = result_functions[key];
    if (value == Node())
      value = Node(node_map_arg, {{"function", Node(cid)},
                                  {"candidates", Node(node_list_arg)}});
    value["candidates"].emplace_back(
        Node(node_map_arg, {{"nodes", nodes},
                            {"callee", Node(callees[m].getCID())},
                            {"caller_savings", s_m[m]},
                            {"callee_size", f_m[m]}}));
    // FIXME: sort candidates.
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

  FunctionAnalysisManager am = makeFAM();
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
  report_fatal_error("This part of BCDB is broken and needs to be updated!");
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

NodeOrCID smout::equivalent_pairs_in_group(Evaluator &evaluator,
                                           NodeRef options, NodeRef mod,
                                           NodeRef group) {
  report_fatal_error("This part of BCDB is broken and needs to be updated!");
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync(candidates_version, options, func_cid));
  }
  std::vector<Future> callees;
  for (auto &future : func_candidates) {
    const CID &func_cid = future.first;
    Future &result = future.second;
    for (auto &item : result->map_range())
      for (auto &candidate : item.value().list_range())
        callees.emplace_back(evaluator.evaluateAsync(
            extracted_callees_version, func_cid, candidate["nodes"]));
    result.freeNode();
  }
  func_candidates.clear();
  StringSet unique_set;
  for (auto &result : callees) {
    result.freeNode();
    unique_set.insert(cid_key(result.getCID()));
  }
  callees.clear();
  std::vector<CID> unique;
  for (const auto &item : unique_set) {
    const auto &key = item.getKey();
    unique.emplace_back(*CID::fromBytes(
        ArrayRef(reinterpret_cast<const uint8_t *>(key.data()), key.size())));
  }
  std::vector<Future> pairs;
  for (size_t i = 0; i < unique.size(); ++i) {
    for (size_t j = 0; j < unique.size(); ++j) {
      if (i == j)
        continue;
      pairs.emplace_back(evaluator.evaluateAsync("alive.tv", Node(node_map_arg),
                                                 unique[i], unique[j]));
    }
  }
  unsigned total = 0;
  for (auto &future : pairs)
    if ((*future)["valid"] == true)
      total++;
  return Node(total);
}

NodeOrCID smout::equivalent_pairs(Evaluator &evaluator, NodeRef options,
                                  NodeRef mod) {
  report_fatal_error("This part of BCDB is broken and needs to be updated!");
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync(candidates_version, options, func_cid));
  }
  StringMap<unsigned> group_count;
  for (auto &future : func_candidates) {
    Future &result = future.second;
    for (auto &item : result->map_range())
      group_count[item.key()]++;
    result.freeNode();
  }
  // TODO: not using evaluateAsync for smout.equivalent_pairs_in_group because
  // there would be too many simultaneous calls, leaving no threads to evaluate
  // smout.extracted.callee.
  unsigned total = 0;
  for (const auto &item : group_count) {
    if (item.getValue() >= 2) {
      auto group_pairs =
          evaluator.evaluate(equivalent_pairs_in_group_version, options, mod,
                             Node(utf8_string_arg, item.getKey()));
      total += group_pairs->as<unsigned>();
    }
  }
  return Node(total);
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
  evaluator.registerFunc(equivalent_pairs_in_group_version,
                         &equivalent_pairs_in_group);
  evaluator.registerFunc(equivalent_pairs_version, &equivalent_pairs);
}
