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
#include "outlining/LinearProgram.h"
#include "outlining/SizeModel.h"

using namespace llvm;
using namespace memodb;
using bcdb::getSoleDefinition;
using bcdb::LinearProgram;
using bcdb::OutliningCalleeExtractor;
using bcdb::OutliningCandidates;
using bcdb::OutliningCandidatesAnalysis;
using bcdb::OutliningDependenceAnalysis;
using bcdb::SizeModelAnalysis;

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
  Node node(node_list_arg, {Node(node_list_arg), Node(node_list_arg)});
  Node &args = node[0];
  Node &results = node[1];
  for (Type *type : candidate.arg_types)
    args.emplace_back(getTypeName(type));
  for (Type *type : candidate.result_types)
    results.emplace_back(getTypeName(type));

  // FIXME: also consider the names of accessed globals.

  std::vector<std::uint8_t> bytes;
  node.save_cbor(bytes);
  return Multibase::base64pad.encodeWithoutPrefix(bytes);
}

NodeOrCID smout::candidates(Evaluator &evaluator, NodeRef options,
                            NodeRef func) {
  ExitOnError Err("smout.candidates: ");
  LLVMContext context;
  auto m = Err(parseBitcodeFile(
      MemoryBufferRef(func->as<StringRef>(byte_string_arg), ""), context));
  Function &f = getSoleDefinition(*m);

  FunctionAnalysisManager am;
  // TODO: Would it be faster to just register the analyses we need?
  PassBuilder().registerFunctionAnalyses(am);
  am.registerPass([] { return OutliningCandidatesAnalysis(); });
  am.registerPass([] { return OutliningDependenceAnalysis(); });
  am.registerPass([] { return SizeModelAnalysis(); });
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

NodeOrCID smout::candidates_total(Evaluator &evaluator, NodeRef options,
                                  NodeRef mod) {
  std::vector<Future> func_candidates;
  for (auto &item : (*mod)["functions"].map_range())
    func_candidates.emplace_back(evaluator.evaluateAsync(
        "smout.candidates", options, item.value().as<CID>()));
  std::size_t total = 0;
  for (auto &future : func_candidates) {
    for (auto &item : future->map_range())
      total += item.value().size();
  }
  return Node(total);
}

NodeOrCID smout::extracted_callee(Evaluator &evaluator, NodeRef func,
                                  NodeRef nodes) {
  ExitOnError Err("smout.extracted.callee: ");
  LLVMContext context;
  auto m = Err(parseBitcodeFile(
      MemoryBufferRef(func->as<StringRef>(byte_string_arg), ""), context));
  Function &f = getSoleDefinition(*m);

  SparseBitVector<> bv = decodeBitVector(*nodes);

  FunctionAnalysisManager am;
  // TODO: Would it be faster to just register the analyses we need?
  PassBuilder().registerFunctionAnalyses(am);
  am.registerPass([] { return OutliningDependenceAnalysis(); });
  auto &deps = am.getResult<OutliningDependenceAnalysis>(f);

  OutliningCalleeExtractor extractor(f, deps, bv);
  Function *callee = extractor.createDefinition();

  bcdb::Splitter splitter(*m);
  auto mpart = splitter.SplitGlobal(callee);
  SmallVector<char, 0> buffer;
  bcdb::WriteAlignedModule(*mpart, buffer);
  return Node(byte_string_arg, buffer);
}

NodeOrCID smout::unique_callees(Evaluator &evaluator,
                                NodeRef candidates_options, NodeRef mod) {
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid, evaluator.evaluateAsync("smout.candidates",
                                          candidates_options, func_cid));
  }
  std::vector<Future> callees;
  for (auto &future : func_candidates) {
    const CID &func_cid = future.first;
    Future &result = future.second;
    for (auto &item : result->map_range())
      for (auto &candidate : item.value().list_range())
        callees.emplace_back(evaluator.evaluateAsync(
            "smout.extracted.callee", func_cid, candidate["nodes"]));
  }
  StringSet unique;
  for (auto &result : callees) {
    auto bytes = result.getCID().asBytes();
    unique.insert(
        StringRef(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
  }
  return Node(unique.size());
}

NodeOrCID smout::ilp_problem(Evaluator &evaluator, NodeRef options,
                             NodeRef mod) {
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync("smout.candidates", options, func_cid));
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
            "smout.extracted.callee", func_cid, candidate["nodes"]));
      }
    }
  }

  StringMap<size_t> callee_indices;
  std::vector<std::optional<LinearProgram::Var>> y_n;
  std::vector<int> f_n;
  std::vector<size_t> callee_for_caller;
  std::vector<int> sum_s_n;
  for (size_t m = 0; m < callees.size(); ++m) {
    auto bytes = callees[m].getCID().asBytes();
    auto key =
        StringRef(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    size_t n;
    if (callee_indices.count(key)) {
      n = callee_indices[key];
      f_n[n] = std::min(f_n[n], f_m[m]);
      sum_s_n[n] += s_m[m];
    } else {
      n = callee_indices[key] = y_n.size();
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
  long free_benefit = 0;
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
  std::vector<std::pair<CID, Future>> func_candidates;
  for (auto &item : (*mod)["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid,
        evaluator.evaluateAsync("smout.candidates", options, func_cid));
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
        callees.emplace_back(evaluator.evaluateAsync("smout.extracted.callee",
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
  }

  // Get extracted callees.
  StringMap<size_t> callee_indices;
  std::vector<int> f_n;
  std::vector<size_t> n_from_m;
  std::vector<SmallVector<size_t, 2>> m_from_n;
  for (size_t m = 0; m < callees.size(); ++m) {
    auto bytes = callees[m].getCID().asBytes();
    auto key =
        StringRef(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    size_t n;
    if (!callee_indices.count(key)) {
      n = callee_indices[key] = callee_indices.size();
      f_n.emplace_back(f_m[m]);
      m_from_n.emplace_back();
    } else {
      n = callee_indices[key];
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
  }
  return Node(node_map_arg, {
                                {"functions", result_functions},
                                {"total_benefit", total_benefit},
                            });
}
