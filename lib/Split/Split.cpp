#include "bcdb/Split.h"

#include "Codes.h"

#include <llvm/Bitcode/LLVMBitCodes.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

using namespace bcdb;
using namespace llvm;

// TODO: function-level inline asm
// TODO: sections
// TODO: GC
// TODO: function prefix, function prologue
// TODO: function alignment
// TODO: function address space
// TODO: parameter attributes, return value attributes

// TODO: function-, instruction-level metadata
// TODO: named arguments, instructions, basic blocks

// TODO: operand bundles
// TODO: sync scopes

// TODO: aliases
// TODO: ifuncs

// TODO: target triple
// TODO: source file name
// TODO: module-level inline asm
// TODO: module-level metadata

static unsigned getEncodedLinkage(const GlobalValue::LinkageTypes Linkage) {
  switch (Linkage) {
  case GlobalValue::ExternalLinkage:
    return codes::LINKAGE_TYPE_EXTERNAL;
  case GlobalValue::AppendingLinkage:
    return codes::LINKAGE_TYPE_APPENDING;
  case GlobalValue::InternalLinkage:
    return codes::LINKAGE_TYPE_INTERNAL;
  case GlobalValue::ExternalWeakLinkage:
    return codes::LINKAGE_TYPE_EXTERNAL_WEAK;
  case GlobalValue::CommonLinkage:
    return codes::LINKAGE_TYPE_COMMON;
  case GlobalValue::PrivateLinkage:
    return codes::LINKAGE_TYPE_PRIVATE;
  case GlobalValue::AvailableExternallyLinkage:
    return codes::LINKAGE_TYPE_AVAILABLE_EXTERNALLY;
  case GlobalValue::WeakAnyLinkage:
    return codes::LINKAGE_TYPE_WEAK_ANY;
  case GlobalValue::WeakODRLinkage:
    return codes::LINKAGE_TYPE_WEAK_ODR;
  case GlobalValue::LinkOnceAnyLinkage:
    return codes::LINKAGE_TYPE_LINK_ONCE_ANY;
  case GlobalValue::LinkOnceODRLinkage:
    return codes::LINKAGE_TYPE_LINK_ONCE_ODR;
  }
  llvm_unreachable("Invalid linkage");
}

static unsigned getEncodedComdatSelectionKind(Comdat::SelectionKind Kind) {
  switch (Kind) {
  case Comdat::Any:
    return bitc::COMDAT_SELECTION_KIND_ANY;
  case Comdat::ExactMatch:
    return bitc::COMDAT_SELECTION_KIND_EXACT_MATCH;
  case Comdat::Largest:
    return bitc::COMDAT_SELECTION_KIND_LARGEST;
  case Comdat::NoDuplicates:
    return bitc::COMDAT_SELECTION_KIND_NO_DUPLICATES;
  case Comdat::SameSize:
    return bitc::COMDAT_SELECTION_KIND_SAME_SIZE;
  }
  llvm_unreachable("Invalid selection kind");
}

// We don't need to change or merge any types.
namespace {
class IdentityTypeMapTy : public ValueMapTypeRemapper {
public:
  Type *get(Type *SrcTy) { return SrcTy; }

  FunctionType *get(FunctionType *T) {
    return cast<FunctionType>(get((Type *)T));
  }

private:
  Type *remapType(Type *SrcTy) override { return get(SrcTy); }
};
}

namespace {
class DeclMaterializer : public ValueMaterializer {
  Module &DM;
  const Module &SM;
  IdentityTypeMapTy &TypeMap;

public:
  DeclMaterializer(Module &DM, const Module &SM, IdentityTypeMapTy &TypeMap)
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
        DM, TypeMap.get(SGVar->getValueType()), SGVar->isConstant(),
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
        DM, TypeMap.get(SGV->getValueType()), /*isConstant*/ false,
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

static std::unique_ptr<Module> ExtractFunction(Module &M, Function &SF) {
  auto MPart = std::make_unique<Module>(SF.getName(), M.getContext());

  // See LLVM's IRLinker::linkFunctionBody().
  Function *DF = Function::Create(
      SF.getFunctionType(), GlobalValue::ExternalLinkage, "", MPart.get());
  DF->copyAttributesFrom(&SF);
  DF->stealArgumentListFrom(SF);
  DF->getBasicBlockList().splice(DF->end(), SF.getBasicBlockList());

  DF->setVisibility(GlobalValue::DefaultVisibility);
  DF->setUnnamedAddr(GlobalValue::UnnamedAddr::None);
  DF->setDLLStorageClass(GlobalValue::DefaultStorageClass);
#if LLVM_VERSION_MAJOR >= 7
  DF->setDSOLocal(false);
#endif

  // Remap all values used within the function.
  ValueToValueMapTy VMap;
  IdentityTypeMapTy TypeMap;
  DeclMaterializer Materializer(*MPart, M, TypeMap);
  RemapFunction(*DF, VMap, RemapFlags::RF_NullMapMissingGlobalValues, &TypeMap,
                &Materializer);

  return MPart;
}

void bcdb::SplitModule(std::unique_ptr<llvm::Module> M, SplitSaver &Saver) {
  // Make sure all globals are named so we can link everything back together
  // later.
  nameUnamedGlobals(*M);

  for (Function &F : *M) {
    if (!F.isDeclaration()) {
      // Create a new module containing only this function.
      auto MPart = ExtractFunction(*M, F);

      unsigned Linkage = getEncodedLinkage(F.getLinkage());
      Comdat *Comdat = F.getComdat();
      StringRef ComdatName = Comdat ? Comdat->getName() : "";
      unsigned ComdatKind =
          Comdat ? getEncodedComdatSelectionKind(Comdat->getSelectionKind())
                 : 0;

      Saver.saveFunction(std::move(MPart), F.getName(), Linkage, ComdatName,
                         ComdatKind);

      // Delete the function from the old module.
      F.deleteBody();
      F.setComdat(nullptr);
    }
  }

  Saver.saveRemainder(std::move(M));
}
