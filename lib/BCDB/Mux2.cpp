#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <map>

#include "Merge.h"
#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

// Handling references from the muxed library to the stub libraries:
// - The muxed library will refer to various symbols that are defined in the
//   stub libraries, but for any given execution, only a subset of the stub
//   libraries will be loaded. So some of these references will be undefined.
// - If we try just leaving these references undefined, both ld and ld.so will
//   error out.
//   - We can convince ld to let us do this by building the muxed library with
//     "-z undefs" and building stub programs with "--allow-shlib-undefined".
//   - For functions, we can convince ld.so to allow this by running ld with
//     "-z lazy".
//   - But for globals, ld.so will always abort if it can't find a definition.
// - Instead we can add weak definitions of everything to the muxed library.
//   - We need to set LD_DYNAMIC_WEAK whenever we run a muxed program, to
//     ensure that the weak definition is overridden by any strong definitions.
// - See also ld's -Bsymbolic option.

class Mux2Merger : public Merger {
public:
  Mux2Merger(BCDB &bcdb);
  ResolvedReference Resolve(StringRef ModuleName, StringRef Name) override;
  void PrepareToRename();
  std::unique_ptr<Module> Finish();

  StringMap<std::unique_ptr<Module>> StubModules;

protected:
  void AddPartStub(Module &MergedModule, GlobalItem &GI, GlobalValue *Def,
                   GlobalValue *Decl, StringRef NewName) override;
  void LoadRemainder(std::unique_ptr<Module> M,
                     std::vector<GlobalItem *> &GIs) override;

private:
  std::vector<GlobalItem *> WeakGlobals;
};

Mux2Merger::Mux2Merger(BCDB &bcdb) : Merger(bcdb) {
  EnableMustTail = true;

  MergedModule->setPICLevel(PICLevel::BigPIC);
  MergedModule->addModuleFlag(Module::Warning, "bcdb.elf.type", ELF::ET_DYN);
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
                             GlobalValue *Def, GlobalValue *Decl,
                             StringRef NewName) {
  Module &StubModule = *StubModules[GI.ModuleName];
  GlobalValue *StubInStubModule = StubModule.getNamedValue(GI.Name);

  // There could be references to this global in both the merged module and the
  // stub module.
  //
  // FIXME: Avoid creating two stub globals in this case.

  if (Decl->hasLocalLinkage())
    Merger::AddPartStub(MergedModule, GI, Def, Decl, NewName);
  else
    WeakGlobals.push_back(&GI);

  if (!Decl->hasLocalLinkage() || !StubInStubModule->use_empty()) {
    LinkageMap[Def] = GlobalValue::ExternalLinkage;
    Function *BodyDecl = StubModule.getFunction(Def->getName());
    if (!BodyDecl) {
      BodyDecl = Function::Create(cast<Function>(Def)->getFunctionType(),
                                  GlobalValue::ExternalLinkage, Def->getName(),
                                  &StubModule);
    }
    assert(BodyDecl->getName() == Def->getName());
    assert(BodyDecl->getFunctionType() ==
           cast<Function>(Def)->getFunctionType());
    Merger::AddPartStub(StubModule, GI, BodyDecl, Decl, GI.Name);
  }
}

void Mux2Merger::LoadRemainder(std::unique_ptr<Module> M,
                               std::vector<GlobalItem *> &GIs) {
  Module &StubModule = *StubModules[M->getModuleIdentifier()];
  std::vector<GlobalItem *> MergedGIs;
  for (GlobalItem *GI : GIs) {
    GlobalValue *GV = M->getNamedValue(GI->NewName);
    if (GV->hasLocalLinkage()) {
      MergedGIs.push_back(GI);
    } else {
      WeakGlobals.push_back(GI);
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      NewGV->setLinkage(GV->getLinkage());

#if LLVM_VERSION_MAJOR >= 7
      NewGV->setDSOLocal(GV->isDSOLocal());
#endif
    }
  }

  eraseModuleFlag(*M, "bcdb.elf.soname");
  eraseModuleFlag(*M, "bcdb.elf.type");
  eraseModuleFlag(*M, "bcdb.elf.flags");
  eraseModuleFlag(*M, "bcdb.elf.flags_1");
  eraseModuleFlag(*M, "bcdb.elf.auxiliary");
  eraseModuleFlag(*M, "bcdb.elf.filter");
  eraseModuleFlag(*M, "bcdb.elf.needed");
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

  // Make weak definitions for everything defined in a stub library that might
  // be used in the muxed library. That way we can link against the muxed
  // library even if we're not linking against that particular stub library.
  for (GlobalItem *GI : WeakGlobals) {
    GlobalValue *GV = M->getNamedValue(GI->NewName);
    if (GV && GV->isDeclaration()) {
      if (GlobalVariable *Var = dyn_cast<GlobalVariable>(GV)) {
        Var->setInitializer(Constant::getNullValue(Var->getValueType()));
      } else if (Function *F = dyn_cast<Function>(GV)) {
        BasicBlock *BB = BasicBlock::Create(F->getContext(), "", F);
        new UnreachableInst(F->getContext(), BB);
      }
      GV->setLinkage(GlobalValue::LinkOnceAnyLinkage);
    }
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
