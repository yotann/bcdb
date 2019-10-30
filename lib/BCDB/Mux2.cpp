#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <map>
#include <set>

#include "Merge.h"
#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

class Mux2Merger : public Merger {
public:
  Mux2Merger(BCDB &bcdb) : Merger(bcdb) {}
  ResolvedReference Resolve(StringRef ModuleName, StringRef Name) override;
  void PrepareToRename();

  StringMap<std::unique_ptr<Module>> StubModules;

protected:
  void AddPartStub(Module &MergedModule, GlobalItem &GI, GlobalValue *Def,
                   GlobalValue *Decl) override;
  void LoadRemainder(Module &MergedModule, std::unique_ptr<Module> M,
                     std::vector<GlobalItem *> &GIs) override;
};

ResolvedReference Mux2Merger::Resolve(StringRef ModuleName, StringRef Name) {
  GlobalValue *GV = ModRemainders[ModuleName]->getNamedValue(Name);
  if (GV && GV->hasLocalLinkage())
    return ResolvedReference(ModuleName, Name);
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
  if (Decl->hasLocalLinkage()) {
    return Merger::AddPartStub(MergedModule, GI, Def, Decl);
  }
  LinkageMap[Def] = GlobalValue::ExternalLinkage;
  Module &StubModule = *StubModules[GI.ModuleName];
  Function *DeclInStubModule = Function::Create(
      cast<Function>(Def)->getFunctionType(), GlobalValue::ExternalLinkage,
      Def->getName(), &StubModule);
  assert(DeclInStubModule->getName() == Def->getName());
  Merger::AddPartStub(StubModule, GI, DeclInStubModule, Decl);
}

void Mux2Merger::LoadRemainder(Module &MergedModule, std::unique_ptr<Module> M,
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
  // FIXME: add merged module to stub module's bcdb.elf.needed
  createGlobalDCEPass()->runOnModule(StubModule);
  Merger::LoadRemainder(MergedModule, std::move(M), MergedGIs);
}

void BCDB::Mux2(std::vector<StringRef> Names) {
  Mux2Merger Merger(*this);
  for (StringRef Name : Names) {
    Merger.AddModule(Name);
  }
  Merger.PrepareToRename();
  Merger.RenameEverything();
  std::map<std::pair<std::string, std::string>, Value *> Mapping;
  errs() << *Merger.Finish(Mapping);
  for (auto &Item : Merger.StubModules) {
    errs() << *Item.second;
  }
}
