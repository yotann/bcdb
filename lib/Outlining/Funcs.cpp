#include "Outlining/Funcs.h"

#include <cstdint>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/ADT/StringRef.h>
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
#include "Outlining/SizeModel.h"
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
using bcdb::SizeModelAnalysis;

static Node encodeBitVector(const SparseBitVector<> &bv) {
  // TODO: Is it worthwhile to compress this by just encoding the number of 0s
  // between each set bit?
  Node result(node_list_arg);
  for (auto i : bv)
    result.emplace_back(i);
  return result;
}

static Node getTypeName(const Type *type) {
  // TODO: cache results.
  Node subtypes(node_list_arg);
  if (!type->isPointerTy())
    for (const Type *subtype : type->subtypes())
      subtypes.push_back(getTypeName(subtype));

  switch (type->getTypeID()) {
    // General primitive types.
  case Type::VoidTyID:
    return 0;
  case Type::LabelTyID:
    return 1;
  case Type::MetadataTyID:
    return 2;
  case Type::TokenTyID:
    return 3;

    // IEEE floats.
  case Type::HalfTyID:
    return 11;
  case Type::FloatTyID:
    return 12;
  case Type::DoubleTyID:
    return 13;
  case Type::FP128TyID:
    return 14;

    // Target-specific primitive types.
  case Type::X86_FP80TyID:
    return -1;
  case Type::PPC_FP128TyID:
    return -2;
  case Type::X86_MMXTyID:
    return -3;
#if LLVM_VERSION_MAJOR >= 11
  case Type::BFloatTyID:
    return -4;
#endif
#if LLVM_VERSION_MAJOR >= 12
  case Type::X86_AMXTyID:
    return -5;
#endif

    // Derived types.
  case Type::IntegerTyID:
    return Node(node_list_arg, {0, type->getIntegerBitWidth()});

  case Type::PointerTyID:
    return Node(node_list_arg,
                {1, std::move(subtypes), type->getPointerAddressSpace()});

  case Type::ArrayTyID:
    return Node(node_list_arg,
                {2, std::move(subtypes), type->getArrayNumElements()});

#if LLVM_VERSION_MAJOR >= 11
  case Type::FixedVectorTyID:
    return Node(node_list_arg, {3, std::move(subtypes),
                                cast<FixedVectorType>(type)->getNumElements()});

  case Type::ScalableVectorTyID:
    return Node(node_list_arg,
                {4, std::move(subtypes),
                 cast<ScalableVectorType>(type)->getMinNumElements()});
#else
  case Type::VectorTyID:
    return Node(node_list_arg, {3, std::move(subtypes),
                                cast<VectorType>(type)->getNumElements()});
#endif

  case Type::StructTyID:
    if (cast<StructType>(type)->isOpaque())
      return 4;
    // We don't care about isLiteral().
    return Node(node_list_arg,
                {5, std::move(subtypes), cast<StructType>(type)->isPacked()});

  case Type::FunctionTyID:
    return Node(node_list_arg,
                {6, std::move(subtypes), cast<FunctionType>(type)->isVarArg()});
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
  OutliningCandidates &candidates =
      am.getResult<OutliningCandidatesAnalysis>(f);

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
