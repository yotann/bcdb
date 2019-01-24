#include "bcdb/Split.h"

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Error.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

using namespace bcdb;
using namespace llvm;

namespace {
/// Remaps types and replaces unneeded named structs with opaque structs.
///
/// 1. Call VisitFunction() to determine which type definitions are actually
///    needed by the function we want to keep.
/// 2. Use the type map to remap all types in the function. Unneeded named
///    structs will be replaced with opaque structs.
/// 3. Save the function.
/// 4. Call RestoreNames() to restore all struct names in the original module.
class NeededTypeMap : public ValueMapTypeRemapper {
  /// Set of types we actually need to keep around.
  SmallPtrSet<const Type *, 16> Needed;

  /// Set of already-visited metadata, to prevent infinite recursion.
  SmallPtrSet<const Metadata *, 16> VisitedMetadata;

  /// Mapping for source types that have already been mapped.
  DenseMap<Type *, Type *> MappedTypes;

  /// List of named structs in the source module that have had their names
  /// temporarily stolen.
  SmallVector<StructType *, 16> StolenNameTypes;

  /// Whether we saw any blockaddress value.
  bool VisitedAnyBlockAddress = false;

public:
  Type *remapType(Type *SrcTy) override { return get(SrcTy); }

  Type *get(Type *Ty) {
    assert(Needed.count(Ty) && "not all types visited");
    return getMember(Ty);
  }

  FunctionType *get(FunctionType *Ty) {
    return cast<FunctionType>(get((Type *)Ty));
  }

  /// Get the mapping for a type that may not have been visited directly.
  Type *getMember(Type *Ty);

  /// Restore struct names to the source module that were stolen for use in the
  /// destination module.
  void RestoreNames();

  /// Visit various parts of the source module to determine which type
  /// definitions we need to keep.
  void VisitType(const Type *Ty);
  void VisitValue(const Value *V);
  void VisitMetadata(const Metadata *MD);
  void VisitInstruction(Instruction *I);
  void VisitFunction(Function &F);

  /// Check whether any blockaddress values were visited.
  bool didVisitAnyBlockAddress() const { return VisitedAnyBlockAddress; }
};
} // end anonymous namespace

Type *NeededTypeMap::getMember(Type *Ty) {
  // See LLVM's TypeMapTy::get().

  Type **Entry = &MappedTypes[Ty];
  if (*Entry)
    return *Entry;
  if (Ty->getNumContainedTypes() == 0)
    return *Entry = Ty;

  bool IsUniqued = !isa<StructType>(Ty) || cast<StructType>(Ty)->isLiteral();
  bool NeedsRenaming = isa<StructType>(Ty) && cast<StructType>(Ty)->hasName();

  // Prevent infinite recursion with a placeholder struct.
  StructType *Placeholder = nullptr;
  bool ForceOpaque = false;
  if (!IsUniqued) {
    *Entry = Placeholder = StructType::create(Ty->getContext());
    if (!Needed.count(Ty))
      ForceOpaque = true;
  }

  SmallVector<Type *, 4> ElementTypes;
  bool AnyChange = false;
  if (!ForceOpaque) {
    // Map the element types.
    ElementTypes.reserve(Ty->getNumContainedTypes());
    for (Type *SubTy : Ty->subtypes()) {
      ElementTypes.push_back(getMember(SubTy));
      AnyChange |= ElementTypes.back() != SubTy;
    }

    Entry = &MappedTypes[Ty];
    // If none of the element types changed, stop and reuse the original type.
    if (!AnyChange && !NeedsRenaming)
      return *Entry = Ty;
  }

  // Create a new type with the mapped element types.
  switch (Ty->getTypeID()) {
  default:
    llvm_unreachable("unknown derived type to remap");
  case Type::ArrayTyID:
    return *Entry = ArrayType::get(ElementTypes[0],
                                   cast<ArrayType>(Ty)->getNumElements());
  case Type::VectorTyID:
    return *Entry = VectorType::get(ElementTypes[0],
                                    cast<VectorType>(Ty)->getNumElements());
  case Type::PointerTyID:
    return *Entry = PointerType::get(ElementTypes[0],
                                     cast<PointerType>(Ty)->getAddressSpace());
  case Type::FunctionTyID:
    return *Entry = FunctionType::get(ElementTypes[0],
                                      makeArrayRef(ElementTypes).slice(1),
                                      cast<FunctionType>(Ty)->isVarArg());
  case Type::StructTyID: {
    auto *STy = cast<StructType>(Ty);
    // For literal struct types, we just create a new literal struct type.
    if (IsUniqued)
      return *Entry = StructType::get(Ty->getContext(), ElementTypes,
                                      STy->isPacked());
    assert(!STy->isOpaque() && "opaque should have been handled already");
    assert(*Entry == Placeholder && "placeholder was replaced");
    // Fill in the placeholder with the mapped element types.
    if (!ForceOpaque)
      Placeholder->setBody(ElementTypes, STy->isPacked());

    // Steal the name from the source type.
    // XXX: disabled (see issue #10).
    if (false && STy->hasName()) {
      SmallString<16> TmpName = STy->getName();
      STy->setName("");
      Placeholder->setName(TmpName);
      StolenNameTypes.push_back(STy);
    }

    return Placeholder;
  }
  }
}

void NeededTypeMap::RestoreNames() {
  for (StructType *STy : StolenNameTypes) {
    StructType *DTy = cast<StructType>(MappedTypes[STy]);
    SmallString<16> TmpName = DTy->getName();
    DTy->setName("");
    STy->setName(TmpName);
  }
}

void NeededTypeMap::VisitType(const Type *Ty) {
  bool New = Needed.insert(Ty).second;
  if (New) {
    // When using a pointer to a named struct type, we don't necessarily need
    // the struct type's definition.
    if (Ty->isPointerTy()) {
      StructType *ST = dyn_cast<StructType>(Ty->getPointerElementType());
      if (ST && !ST->isLiteral())
        return;
    }
    // Otherwise, we need definitions for all subtypes.
    for (const Type *SubTy : Ty->subtypes())
      VisitType(SubTy);
  }
}

void NeededTypeMap::VisitValue(const Value *V) {
  // See LLVM's Mapper::mapValue().

  if (const InlineAsm *IA = dyn_cast<InlineAsm>(V))
    return VisitType(IA->getFunctionType());

  if (const auto *MDV = dyn_cast<MetadataAsValue>(V))
    return VisitMetadata(MDV->getMetadata());

  if (const Constant *C = dyn_cast<Constant>(V)) {
    VisitType(V->getType());

    if (isa<BlockAddress>(C))
      VisitedAnyBlockAddress = true;

    if (!isa<GlobalValue>(C))
      for (const Use &Op : C->operands())
        VisitValue(Op);

    if (auto *GEPO = dyn_cast<GEPOperator>(C))
      VisitType(GEPO->getSourceElementType());

    if (auto *F = dyn_cast<Function>(C)) {
      for (auto &Arg : F->args())
        if (Arg.hasByValOrInAllocaAttr())
          VisitType(Arg.getType()->getPointerElementType());
    }
  }
}

void NeededTypeMap::VisitMetadata(const Metadata *MD) {
  bool New = VisitedMetadata.insert(MD).second;
  if (!New)
    return;
  if (auto *VMD = dyn_cast<ValueAsMetadata>(MD))
    return VisitValue(VMD->getValue());
  if (auto *N = dyn_cast<MDNode>(MD)) {
    for (auto &Op : N->operands())
      VisitMetadata(Op);
  }
}

void NeededTypeMap::VisitInstruction(Instruction *I) {
  // See LLVM's Mapper::remapInstruction().

  VisitType(I->getType());
  for (const Use &Op : I->operands())
    VisitValue(Op);

  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  I->getAllMetadata(MDs);
  for (const auto &MI : MDs)
    VisitMetadata(MI.second);

  if (auto *AI = dyn_cast<AllocaInst>(I))
    VisitType(AI->getAllocatedType());
  if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
    VisitType(GEP->getSourceElementType());
}

void NeededTypeMap::VisitFunction(Function &F) {
  // See LLVM's Mapper::remapFunction().

  VisitType(F.getType());
  for (const Use &Op : F.operands())
    VisitValue(Op);

  SmallVector<std::pair<unsigned, MDNode *>, 8> MDs;
  F.getAllMetadata(MDs);
  for (const auto &MI : MDs)
    VisitMetadata(MI.second);

  for (const Argument &A : F.args())
    VisitType(A.getType());
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      VisitInstruction(&I);
}

namespace {
class DeclMaterializer : public ValueMaterializer {
  Module &DM;
  const Module &SM;
  NeededTypeMap &TypeMap;

public:
  DeclMaterializer(Module &DM, const Module &SM, NeededTypeMap &TypeMap)
      : DM(DM), SM(SM), TypeMap(TypeMap) {}
  Value *materialize(Value *V) override;
};
} // end anonymous namespace

Value *DeclMaterializer::materialize(Value *V) {
  // See LLVM's IRLinker::copyGlobalValueProto().
  const GlobalValue *SGV = dyn_cast<GlobalValue>(V);
  if (!SGV)
    return nullptr;

  GlobalValue *NewGV;
  if (auto *SGVar = dyn_cast<GlobalVariable>(SGV)) {
    auto *DGVar = new GlobalVariable(
        DM, TypeMap.getMember(SGVar->getValueType()), SGVar->isConstant(),
        GlobalValue::ExternalLinkage, /*init*/ nullptr, SGVar->getName(),
        /*insertbefore*/ nullptr, SGVar->getThreadLocalMode(),
        SGVar->getType()->getAddressSpace());
    DGVar->setAlignment(SGVar->getAlignment());
    DGVar->copyAttributesFrom(SGVar);
    NewGV = DGVar;
  } else if (auto *SF = dyn_cast<Function>(SGV)) {
    auto *DF =
        Function::Create(TypeMap.get(SF->getFunctionType()),
                         GlobalValue::ExternalLinkage, SF->getName(), &DM);
    DF->copyAttributesFrom(SF);
    NewGV = DF;
  } else if (SGV->getValueType()->isFunctionTy()) {
    NewGV =
        Function::Create(cast<FunctionType>(TypeMap.get(SGV->getValueType())),
                         GlobalValue::ExternalLinkage, SGV->getName(), &DM);
  } else {
    NewGV = new GlobalVariable(
        DM, TypeMap.getMember(SGV->getValueType()), /*isConstant*/ false,
        GlobalValue::ExternalLinkage, /*init*/ nullptr, SGV->getName(),
        /*insertbefore*/ nullptr, SGV->getThreadLocalMode(),
        SGV->getType()->getAddressSpace());
  }

  NewGV->setVisibility(GlobalValue::DefaultVisibility);
  NewGV->setUnnamedAddr(GlobalValue::UnnamedAddr::None);
  NewGV->setDLLStorageClass(GlobalValue::DefaultStorageClass);
#if LLVM_VERSION_MAJOR >= 7
  NewGV->setDSOLocal(false);
#endif
  if (SGV->hasExternalWeakLinkage())
    NewGV->setLinkage(GlobalValue::ExternalWeakLinkage);

  if (auto *NewF = dyn_cast<Function>(NewGV)) {
    NewF->setPersonalityFn(nullptr);
    NewF->setPrefixData(nullptr);
    NewF->setPrologueData(nullptr);
  }

  return NewGV;
}

static std::unique_ptr<Module> ExtractFunction(Module &M, Function &SF,
                                               NeededTypeMap &TypeMap) {
  auto MPart = std::make_unique<Module>(SF.getName(), M.getContext());
  MPart->setSourceFileName("");
  // Include datalayout and triple, needed for compilation.
  MPart->setDataLayout(M.getDataLayout());
  MPart->setTargetTriple(M.getTargetTriple());

  TypeMap.VisitFunction(SF);

  // We can't handle blockaddress values in splitted functions.
  if (TypeMap.didVisitAnyBlockAddress())
    return 0;

// See LLVM's IRLinker::linkFunctionBody().
#if LLVM_VERSION_MAJOR > 7
  assert(SF.getAddressSpace() == 0 && "function in non-default address space");
#endif
  Function *DF =
      Function::Create(TypeMap.get(SF.getFunctionType()),
                       GlobalValue::ExternalLinkage, "", MPart.get());
  DF->stealArgumentListFrom(SF);
  DF->getBasicBlockList().splice(DF->end(), SF.getBasicBlockList());

  // Copy attributes.
  // Calling convention, GC, and alignment are kept on both functions.
  DF->copyAttributesFrom(&SF);
  // Personality, prefix, and prologue are only kept on the full function.
  SF.setPersonalityFn(nullptr);
  SF.setPrefixData(nullptr);
  SF.setPrologueData(nullptr);

  // Metadata is only kept on the full function.
  DF->copyMetadata(&SF, /*Offset*/ 0);
  SF.clearMetadata();

  // Linkage information is only kept on the stub.
  DF->setVisibility(GlobalValue::DefaultVisibility);
  DF->setUnnamedAddr(GlobalValue::UnnamedAddr::None);
  DF->setDLLStorageClass(GlobalValue::DefaultStorageClass);
  DF->setSection("");
#if LLVM_VERSION_MAJOR >= 7
  DF->setDSOLocal(false);
#endif

  // Remap all values used within the function.
  ValueToValueMapTy VMap;
  VMap[&SF] = DF; // Map recursive calls to recursive calls.
  DeclMaterializer Materializer(*MPart, M, TypeMap);
  RemapFunction(*DF, VMap, RemapFlags::RF_None, &TypeMap, &Materializer);

  // Add a stub definition to the remainder module so we can keep the
  // linkage type, comdats, and aliases.
  BasicBlock *BB = BasicBlock::Create(SF.getContext(), "", &SF);
  new UnreachableInst(SF.getContext(), BB);

  return MPart;
}

Error bcdb::SplitModule(std::unique_ptr<llvm::Module> M, SplitSaver &Saver) {
  // Make sure all globals are named so we can link everything back together
  // later.
  nameUnamedGlobals(*M);

  for (Function &F : *M) {
    if (!F.isDeclaration()) {
      // We can't handle blockaddress yet.
      if (any_of(F.users(), [](const User *U) { return isa<BlockAddress>(U); }))
        continue;

      // Create a new module containing only this function.
      NeededTypeMap TypeMap;
      auto MPart = ExtractFunction(*M, F, TypeMap);
      if (MPart) {
        if (Error Err = Saver.saveFunction(std::move(MPart), F.getName()))
          return Err;
      }
      TypeMap.RestoreNames();
    }
  }

  return Saver.saveRemainder(std::move(M));
}
