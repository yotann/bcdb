#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#if LLVM_VERSION_MAJOR >= 5
#include <llvm/BinaryFormat/ELF.h>
#else
#include <llvm/Support/ELF.h>
#endif
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <map>

#include "Merge.h"
#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

class Mux2Merger : public Merger {
public:
  Mux2Merger(BCDB &bcdb);
  ResolvedReference Resolve(StringRef ModuleName, StringRef Name) override;
  void PrepareToRename();
  std::unique_ptr<Module> Finish();

  StringMap<std::unique_ptr<Module>> StubModules;

protected:
  void AddPartStub(Module &MergedModule, GlobalItem &GI, GlobalValue *Def,
                   GlobalValue *Decl) override;
  void LoadRemainder(std::unique_ptr<Module> M,
                     std::vector<GlobalItem *> &GIs) override;
};

Mux2Merger::Mux2Merger(BCDB &bcdb) : Merger(bcdb) {
  MergedModule->setPICLevel(PICLevel::BigPIC);
  MergedModule->addModuleFlag(Module::Warning, "bcdb.elf.type", ELF::ET_DYN);
  NamedMDNode *NMD =
      MergedModule->getOrInsertNamedMetadata("bcdb.linker.options");
  NMD->addOperand(MDTuple::get(
      bcdb.GetContext(), {MDString::get(bcdb.GetContext(), "-zundefs"),
                          MDString::get(bcdb.GetContext(), "-Bsymbolic")}));
}

ResolvedReference Mux2Merger::Resolve(StringRef ModuleName, StringRef Name) {
  GlobalValue *GV = ModRemainders[ModuleName]->getNamedValue(Name);
  if (GV && GV->hasLocalLinkage())
    return ResolvedReference(&GlobalItems[GV]);
  else
    return ResolvedReference(Name);
}

void Mux2Merger::PrepareToRename() {
  for (auto &Item : ModRemainders) {
    Item.second->setModuleIdentifier(Item.first());
    ValueToValueMapTy VMap;
    std::unique_ptr<Module> M = CloneModule(*Item.second);
    // Make all definitions internal by default, since the actual definition
    // will probably be in the merged module. That will be changed in
    // LoadRemainder if necessary.
    for (GlobalValue &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      if (!GV.isDeclaration())
        GV.setLinkage(GlobalValue::InternalLinkage);
    }

    NamedMDNode *NMD = M->getOrInsertNamedMetadata("bcdb.linker.options");
    NMD->addOperand(MDTuple::get(
        bcdb.GetContext(),
        {MDString::get(bcdb.GetContext(), "--allow-shlib-undefined")}));

    StubModules[Item.first()] = std::move(M);
  }
  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;
    // If the stub will go into a stub ELF, we keep the existing name. We do
    // NOT add it to ReservedNames, because it isn't going into the merged
    // module.
    if (!GV->hasLocalLinkage()) {
      GI.NewName = GI.Name;
    }
  }
}

void Mux2Merger::AddPartStub(Module &MergedModule, GlobalItem &GI,
                             GlobalValue *Def, GlobalValue *Decl) {
  Module &StubModule = *StubModules[GI.ModuleName];
  GlobalValue *StubInStubModule = StubModule.getNamedValue(GI.Name);

  // There could be references to this global in both the merged module and the
  // stub module.
  //
  // FIXME: Avoid creating two stub globals in this case.

  if (Decl->hasLocalLinkage())
    Merger::AddPartStub(MergedModule, GI, Def, Decl);
  if (!Decl->hasLocalLinkage() || !StubInStubModule->use_empty()) {
    LinkageMap[Def] = GlobalValue::ExternalLinkage;
    Function *DeclInStubModule = Function::Create(
        cast<Function>(Def)->getFunctionType(), GlobalValue::ExternalLinkage,
        Def->getName(), &StubModule);
    assert(DeclInStubModule->getName() == Def->getName());
    Merger::AddPartStub(StubModule, GI, DeclInStubModule, Decl);
  }
}

void Mux2Merger::LoadRemainder(std::unique_ptr<Module> M,
                               std::vector<GlobalItem *> &GIs) {
  Module &StubModule = *StubModules[M->getModuleIdentifier()];
  std::vector<GlobalItem *> MergedGIs;
  for (GlobalItem *GI : GIs) {
    GlobalValue *GV = M->getNamedValue(GI->Name);
    if (GV->hasLocalLinkage()) {
      MergedGIs.push_back(GI);
    } else {
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      NewGV->setLinkage(GV->getLinkage());

#if LLVM_VERSION_MAJOR >= 7
      NewGV->setDSOLocal(GV->isDSOLocal());
#endif
    }
  }

  Merger::LoadRemainder(std::move(M), MergedGIs);
}

std::unique_ptr<Module> Mux2Merger::Finish() {
  auto M = Merger::Finish();

  for (auto &Item : StubModules) {
    Module &StubModule = *Item.second;
    // Prevent deletion of linkonce globals--they may be needed by the muxed
    // module.
    for (GlobalValue &GV :
         concat<GlobalValue>(StubModule.global_objects(), StubModule.aliases(),
                             StubModule.ifuncs()))
      if (GV.hasLinkOnceLinkage())
        GV.setLinkage(GlobalValue::getWeakLinkage(GV.hasLinkOnceODRLinkage()));

    createGlobalDCEPass()->runOnModule(StubModule);
  }

  return M;
}

std::unique_ptr<llvm::Module>
BCDB::Mux2(std::vector<llvm::StringRef> Names,
           llvm::StringMap<std::unique_ptr<llvm::Module>> &Stubs) {
  Mux2Merger Merger(*this);
  for (StringRef Name : Names) {
    Merger.AddModule(Name);
  }
  Merger.PrepareToRename();
  Merger.RenameEverything();
  auto Result = Merger.Finish();

  Stubs = std::move(Merger.StubModules);
  return Result;
}
