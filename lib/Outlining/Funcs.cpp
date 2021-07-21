#include "Outlining/Funcs.h"

#include <cstdint>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/Passes/PassBuilder.h>
#include <string>
#include <vector>

#include "Outlining/Candidates.h"
#include "Outlining/Dependence.h"
#include "Outlining/Extractor.h"
#include "Outlining/SizeModel.h"
#include "bcdb/AlignBitcode.h"
#include "bcdb/Split.h"
#include "memodb/Evaluator.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"
#include "memodb/Store.h"

using namespace llvm;
using namespace memodb;
using bcdb::getSoleDefinition;
using bcdb::OutliningCandidates;
using bcdb::OutliningCandidatesAnalysis;
using bcdb::OutliningDependenceAnalysis;
using bcdb::OutliningExtractor;
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

Node smout::candidates(Evaluator &evaluator, const Node &func) {
  ExitOnError Err("smout.candidates: ");
  LLVMContext context;
  auto m = Err(parseBitcodeFile(
      MemoryBufferRef(func.as<StringRef>(byte_string_arg), ""), context));
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
                               {"fixed_overhead", candidate.fixed_overhead},
                               {"savings_per_copy", candidate.savings_per_copy},
                           }));
  }
  return result;
}

Node smout::candidates_total(Evaluator &evaluator, const Node &mod) {
  std::vector<std::shared_future<NodeRef>> futures;
  for (auto &item : mod["functions"].map_range())
    futures.emplace_back(
        evaluator.evaluateAsync("smout.candidates", item.value().as<CID>()));
  std::size_t total = 0;
  for (auto &future : futures) {
    const NodeRef &result = future.get();
    for (auto &item : result->map_range())
      total += item.value().size();
  }
  return total;
}

Node smout::extracted_callee(Evaluator &evaluator, const Node &func,
                             const Node &nodes) {
  ExitOnError Err("smout.extracted.callee: ");
  LLVMContext context;
  auto m = Err(parseBitcodeFile(
      MemoryBufferRef(func.as<StringRef>(byte_string_arg), ""), context));
  Function &f = getSoleDefinition(*m);

  SparseBitVector<> bv = decodeBitVector(nodes);

  FunctionAnalysisManager am;
  // TODO: Would it be faster to just register the analyses we need?
  PassBuilder().registerFunctionAnalyses(am);
  am.registerPass([] { return OutliningDependenceAnalysis(); });
  auto &deps = am.getResult<OutliningDependenceAnalysis>(f);

  OutliningExtractor extractor(f, deps, bv);
  Function *callee = extractor.createNewCallee();

  bcdb::Splitter splitter(*m);
  auto mpart = splitter.SplitGlobal(callee);
  SmallVector<char, 0> buffer;
  bcdb::WriteAlignedModule(*mpart, buffer);
  return Node(byte_string_arg, buffer);
}

Node smout::unique_callees(Evaluator &evaluator, const Node &mod) {
  std::vector<std::pair<CID, std::shared_future<NodeRef>>> func_candidates;
  for (auto &item : mod["functions"].map_range()) {
    auto func_cid = item.value().as<CID>();
    func_candidates.emplace_back(
        func_cid, evaluator.evaluateAsync("smout.candidates", func_cid));
  }
  std::vector<std::shared_future<NodeRef>> callees;
  for (auto &future : func_candidates) {
    const CID &func_cid = future.first;
    const NodeRef &result = future.second.get();
    for (auto &item : result->map_range()) {
      for (auto &candidate : item.value().list_range()) {
        const CID nodes_cid = evaluator.getStore().put(candidate["nodes"]);
        callees.emplace_back(evaluator.evaluateAsync("smout.extracted.callee",
                                                     func_cid, nodes_cid));
      }
    }
  }
  StringSet unique;
  for (auto &future : callees) {
    const NodeRef &result = future.get();
    auto bytes = result.getCID().asBytes();
    unique.insert(
        StringRef(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
  }
  return unique.size();
}
