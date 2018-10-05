#include "bcdb/Split.h"

#include "Codes.h"

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
  MPart->setSourceFileName("");
  MPart->setDataLayout(M.getDataLayout());
  MPart->setTargetTriple(M.getTargetTriple());

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

      Saver.saveFunction(std::move(MPart), F.getName());

      // Add a stub definition to the remainder module so we can keep the
      // linkage type, comdats, and aliases.
      BasicBlock *BB = BasicBlock::Create(F.getContext(), "", &F);
      new UnreachableInst(F.getContext(), BB);
    }
  }

  Saver.saveRemainder(std::move(M));
}
