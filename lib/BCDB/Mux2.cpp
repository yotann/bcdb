#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <map>
#include <set>

#include "Merge.h"
#include "bcdb/LLVMCompat.h"

namespace {
#include "mux2_library.inc"
}

using namespace bcdb;
using namespace llvm;

static cl::opt<bool> AllowSpuriousExports(
    "allow-spurious-exports",
    cl::desc("Allow the muxed module to export extra symbols that the original "
             "modules didn't export"),
    cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<bool>
    NoUnmuxedDefs("no-unmuxed-defs",
                  cl::desc("Assume no symbol imported by a muxed module is "
                           "exported by a module that isn't muxed"),
                  cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<bool>
    NoUnmuxedUsers("no-unmuxed-users",
                   cl::desc("Assume no symbol exported by a muxed module is "
                            "imported by a module that isn't muxed"),
                   cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static std::unique_ptr<Module> LoadMuxLibrary(LLVMContext &Context) {
  ExitOnError Err("LoadMuxLibrary: ");
  StringRef Buffer(reinterpret_cast<char *>(mux2_library_bc),
                   mux2_library_bc_len);
  auto MainMod =
      Err(parseBitcodeFile(MemoryBufferRef(Buffer, "main"), Context));
  MainMod->setTargetTriple("");
  return MainMod;
}

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
//   - Also, lazy functions don't work if the address of the function is used
//     in a global variable initializer.
// - Instead we can add weak definitions of everything to the muxed library.
//   - We need to set LD_DYNAMIC_WEAK whenever we run a muxed program, to
//     ensure that the weak definition is overridden by any strong definitions.
//   - This still doesn't work right in some cases.
// - Instead we can add weak definitions of everything to a separate "weak
//   library", and ensure that that library is the last thing loaded by the
//   dynamic linker, so it's checked for symbols last.
// - See also ld's -Bsymbolic option.

class Mux2Merger : public Merger {
public:
  Mux2Merger(BCDB &bcdb, bool enable_weak_module);
  ResolvedReference Resolve(StringRef ModuleName, StringRef Name) override;
  void PrepareToRename();
  std::unique_ptr<Module> Finish();

  StringMap<std::unique_ptr<Module>> StubModules;
  std::unique_ptr<Module> WeakModule;

protected:
  void AddPartStub(Module &MergedModule, GlobalItem &GI, GlobalValue *Def,
                   GlobalValue *Decl, StringRef NewName) override;
  void LoadRemainder(std::unique_ptr<Module> M,
                     std::vector<GlobalItem *> &GIs) override;

private:
  bool ShouldDefineInMuxedModule(GlobalItem &GI, GlobalValue *Decl);

  std::map<std::string, int> ExportedCount;
  StringMap<GlobalItem *> ExportedItems;
  std::set<GlobalItem *> DirectlyReferenced;
  StringSet<> IndirectlyReferenced;
};

Mux2Merger::Mux2Merger(BCDB &bcdb, bool enable_weak_module) : Merger(bcdb) {
  EnableMustTail = true;
  EnableNameReuse = false;

  if (enable_weak_module)
    WeakModule = std::make_unique<Module>("weak", bcdb.GetContext());

  MergedModule->setPICLevel(PICLevel::BigPIC);
  MergedModule->addModuleFlag(Module::Warning, "bcdb.elf.type", ELF::ET_DYN);
}

ResolvedReference Mux2Merger::Resolve(StringRef ModuleName, StringRef Name) {
  GlobalValue *GV = ModRemainders[ModuleName]->getNamedValue(Name);
  if (GV && GV->hasExactDefinition()) {
    assert(GlobalItems.count(GV));
    return ResolvedReference(&GlobalItems[GV]);
  } else if (NoUnmuxedDefs && ExportedCount[Name] == 1) {
    assert(ExportedItems.count(Name));
    return ResolvedReference(ExportedItems[Name]);
  } else {
    assert(!Name.empty());
    return ResolvedReference(Name);
  }
}

void Mux2Merger::PrepareToRename() {
  for (auto &Item : ModRemainders) {
    Item.second->setModuleIdentifier(Item.first());
    std::unique_ptr<Module> M = CloneModule(*Item.second);
    // Make all definitions internal by default, since the actual definition
    // will probably be in the merged module. That will be changed in
    // LoadRemainder if necessary.
    for (GlobalValue &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      if (!GV.isDeclarationForLinker())
        GV.setLinkage(GlobalValue::InternalLinkage);
    }
    StubModules[Item.first()] = std::move(M);
  }

  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;
    if (!GV->hasLocalLinkage()) {
      ExportedCount[GI.Name]++;
      ExportedItems[GI.Name] = &GI;
    }
  }

  ExportedCount["main"] = 2;
  ExportedCount["llvm.used"] = 2;
  ExportedCount["llvm.compiler.used"] = 2;
  ExportedCount["llvm.global_ctors"] = 2;
  ExportedCount["llvm.global_dtors"] = 2;

  for (auto &Item : GlobalItems) {
    GlobalItem &GI = Item.second;
    for (auto &Ref : GI.Refs) {
      auto Res = Resolve(GI.ModuleName, Ref.first);
      if (Res.GI) {
        DirectlyReferenced.insert(Res.GI);
      } else {
        ExportedCount[Res.Name] = 2;
        IndirectlyReferenced.insert(Res.Name);
      }
    }
  }

  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;
    if (GlobalAlias *GA = dyn_cast<GlobalAlias>(GV))
      if (ExportedCount[GI.Name] > 1)
        ExportedCount[GA->getAliasee()->getName()] = 2;
  }

  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;
    if (GlobalAlias *GA = dyn_cast<GlobalAlias>(GV))
      if (ExportedCount[GA->getAliasee()->getName()] > 1)
        ExportedCount[GI.Name] = 2;
  }

  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;
    if (!GV->hasLocalLinkage()) {
      // The stub function will go into a stub module.
      if (ExportedCount[GI.Name] > 1 && DirectlyReferenced.count(&GI) &&
          WeakModule) {
        // Add an alias, so we can add an available_externally copy to the
        // merged library and use it for direct references.
        GI.NewName = ReserveName("__bcdb_direct_" + GI.Name);
      } else {
        // Keep the existing name.
        GI.NewName = GI.Name;
        ReservedNames.insert(GI.NewName);
      }
    } else {
      Module &StubModule = *StubModules[GI.ModuleName];
      GlobalValue *StubInStubModule = StubModule.getNamedValue(GI.Name);
      if (!StubInStubModule->use_empty()) {
        // Rename private globals so that we can define them in the merged
        // module, export them, and use them in the stub module.
        GI.NewName = ReserveName("__bcdb_private_" + GI.Name);
      }
    }
  }
}

bool Mux2Merger::ShouldDefineInMuxedModule(GlobalItem &GI, GlobalValue *Decl) {
  if (Decl->hasLocalLinkage())
    return true;
  if (AllowSpuriousExports || NoUnmuxedUsers)
    if (ExportedCount[GI.Name] == 1 && !IndirectlyReferenced.count(GI.Name))
      return true;
  return false;
}

void Mux2Merger::AddPartStub(Module &MergedModule, GlobalItem &GI,
                             GlobalValue *Def, GlobalValue *Decl,
                             StringRef NewName) {
  if (NewName.empty())
    NewName = GI.NewName;
  Module &StubModule = *StubModules[GI.ModuleName];
  GlobalValue *StubInStubModule = StubModule.getNamedValue(GI.Name);

  if (ShouldDefineInMuxedModule(GI, Decl)) {
    Merger::AddPartStub(MergedModule, GI, Def, Decl, NewName);

    if (!StubInStubModule->use_empty()) {
      GlobalValue *NewStub = MergedModule.getNamedValue(NewName);
      LinkageMap[NewStub] = GlobalValue::ExternalLinkage;
      NewStub->setLinkage(GlobalValue::ExternalLinkage);
      NewStub->setVisibility(GlobalValue::ProtectedVisibility);

      StubInStubModule->setName(NewName);
      LinkageMap[StubInStubModule] = GlobalValue::ExternalLinkage;
      StubInStubModule->setLinkage(GlobalValue::ExternalLinkage);
      cast<Function>(StubInStubModule)->deleteBody();
      cast<Function>(StubInStubModule)->setComdat(nullptr);
#if LLVM_VERSION_MAJOR >= 7
      StubInStubModule->setDSOLocal(false);
#endif
    }
  } else {
    // Export the body from the muxed library.
    LinkageMap[Def] = GlobalValue::ExternalLinkage;
    Def->setLinkage(GlobalValue::ExternalLinkage);
    Def->setVisibility(GlobalValue::ProtectedVisibility);

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

    if (GI.Name != GI.NewName) {
      GlobalAlias::create(GlobalValue::ExternalLinkage, GI.NewName,
                          StubModule.getNamedValue(GI.Name));
    }

    if (WeakModule && DirectlyReferenced.count(&GI)) {
      Merger::AddPartStub(MergedModule, GI, Def, Decl, GI.NewName);
      GlobalValue *NewStub = MergedModule.getNamedValue(NewName);
      LinkageMap[NewStub] = GlobalValue::AvailableExternallyLinkage;
      cast<Function>(NewStub)->setComdat(nullptr);
      NewStub->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
      NewStub->setDSOLocal(false);
#endif
    }
  }
}

void Mux2Merger::LoadRemainder(std::unique_ptr<Module> M,
                               std::vector<GlobalItem *> &GIs) {
  Module &StubModule = *StubModules[M->getModuleIdentifier()];
  std::vector<GlobalItem *> MergedGIs;
  for (GlobalItem *GI : GIs) {
    GlobalValue *GV = M->getNamedValue(GI->NewName);
    if (ShouldDefineInMuxedModule(*GI, GV)) {
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      NewGV->setName(GI->NewName);
      NewGV->setLinkage(GlobalValue::AvailableExternallyLinkage);
      if (GlobalObject *NewGO = dyn_cast<GlobalObject>(NewGV))
        NewGO->setComdat(nullptr);
      NewGV->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
      NewGV->setDSOLocal(false);
#endif
      if (isa<GlobalAlias>(NewGV)) {
        Type *Type = NewGV->getValueType();
        GlobalValue *NewGV2;
        if (FunctionType *FType = dyn_cast<FunctionType>(Type)) {
          NewGV2 = Function::Create(FType, GlobalValue::ExternalLinkage, "",
                                    &StubModule);
        } else {
          GlobalVariable *Base =
              cast<GlobalVariable>(cast<GlobalAlias>(NewGV)->getBaseObject());
          NewGV2 = new GlobalVariable(StubModule, Type, Base->isConstant(),
                                      GlobalValue::ExternalLinkage, nullptr);
          NewGV2->setThreadLocalMode(Base->getThreadLocalMode());
        }
        std::string Name = NewGV->getName();
        NewGV->replaceAllUsesWith(NewGV2);
        NewGV->eraseFromParent();
        NewGV2->setName(Name);
      }

      MergedGIs.push_back(GI);
      if (!NewGV->use_empty()) {
        // Define private globals in the merged module, but export them so the
        // stub module can use them.
        GV->setLinkage(GlobalValue::ExternalLinkage);
        GV->setVisibility(GlobalValue::ProtectedVisibility);
      }

    } else {
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      NewGV->setLinkage(GV->getLinkage());
#if LLVM_VERSION_MAJOR >= 7
      NewGV->setDSOLocal(GV->isDSOLocal());
#endif

      if (GI->Name != GI->NewName) {
        GlobalAlias::create(GlobalValue::ExternalLinkage, GI->NewName, NewGV);
      }

      if (WeakModule && DirectlyReferenced.count(GI) && isa<GlobalObject>(GV)) {
        // TODO: handle aliases too.
        MergedGIs.push_back(GI);
        GV->setLinkage(GlobalValue::AvailableExternallyLinkage);
        cast<GlobalObject>(GV)->setComdat(nullptr);
        GV->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
        GV->setDSOLocal(false);
#endif
      }
    }
  }

  M->setModuleInlineAsm("");
  eraseModuleFlag(*M, "PIC Level");
  eraseModuleFlag(*M, "PIE Level");
  eraseModuleFlag(*M, "bcdb.elf.auxiliary");
  eraseModuleFlag(*M, "bcdb.elf.filter");
  eraseModuleFlag(*M, "bcdb.elf.flags");
  eraseModuleFlag(*M, "bcdb.elf.flags_1");
  eraseModuleFlag(*M, "bcdb.elf.needed");
  eraseModuleFlag(*M, "bcdb.elf.rpath");
  eraseModuleFlag(*M, "bcdb.elf.soname");
  eraseModuleFlag(*M, "bcdb.elf.type");
  Merger::LoadRemainder(std::move(M), MergedGIs);
}

static void DiagnoseUnreachableFunctions(Module &M,
                                         FunctionType *UndefFuncType) {
  auto UndefFuncCalled = M.getOrInsertFunction(
      "__bcdb_unreachable_function_called", UndefFuncType);
  for (Function &F : M.functions()) {
    if (!F.isDeclaration() &&
        F.front().front().getOpcode() == Instruction::Unreachable) {
      IRBuilder<> Builder(&F.front().front());
      Builder.CreateCall(UndefFuncCalled,
                         {Builder.CreateGlobalStringPtr(F.getName())});
    }
  }
}

std::unique_ptr<Module> Mux2Merger::Finish() {
  auto M = Merger::Finish();

  Linker::linkModules(*M, LoadMuxLibrary(M->getContext()));
  FunctionType *UndefFuncType =
      M->getFunction("__bcdb_unreachable_function_called")->getFunctionType();
  Function *WeakDefCalled;
  if (WeakModule)
    WeakDefCalled =
        Function::Create(UndefFuncType, GlobalValue::ExternalLinkage,
                         "__bcdb_weak_definition_called", WeakModule.get());

  for (auto &Item : StubModules) {
    Module &StubModule = *Item.second;

    // Prevent deletion of linkonce globals--they may be needed by the muxed
    // module.
    for (GlobalValue &GV :
         concat<GlobalValue>(StubModule.global_objects(), StubModule.aliases(),
                             StubModule.ifuncs()))
      if (GV.hasLinkOnceLinkage())
        GV.setLinkage(GlobalValue::getWeakLinkage(GV.hasLinkOnceODRLinkage()));

    // Make weak symbols strong, to prevent them being overridden by the weak
    // definitions in the muxed library.
    for (GlobalValue &GV :
         concat<GlobalValue>(StubModule.global_objects(), StubModule.aliases(),
                             StubModule.ifuncs()))
      if (GV.hasLinkOnceLinkage() || GV.hasWeakLinkage())
        GV.setLinkage(GlobalValue::ExternalLinkage);

    createGlobalDCEPass()->runOnModule(StubModule);
  }

  // Make weak definitions for everything defined in a stub library or an
  // external library. That way we can link against the muxed library even if
  // we're not linking against that particular stub library.
  for (GlobalObject &GO : M->global_objects()) {
    if (!GO.isDeclarationForLinker())
      continue;

    if (GlobalVariable *Var = dyn_cast<GlobalVariable>(&GO)) {
      if (!WeakModule) {
        Var->setInitializer(nullptr);
        Var->setComdat(nullptr);
      }
      if (Var->isDeclaration()) {
        Var->setLinkage(GlobalValue::ExternalWeakLinkage);
        Var->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
        Var->setDSOLocal(false);
#endif
      } else {
        new GlobalVariable(*WeakModule, Var->getValueType(), Var->isConstant(),
                           GlobalValue::WeakAnyLinkage,
                           Constant::getNullValue(Var->getValueType()),
                           Var->getName(), nullptr, Var->getThreadLocalMode(),
#if LLVM_VERSION_MAJOR >= 8
                           Var->getAddressSpace(),
#endif
                           false);
      }
    } else if (Function *F = dyn_cast<Function>(&GO)) {
      if (F->isIntrinsic())
        continue;
      if (!WeakModule) {
        F->deleteBody();
        F->setComdat(nullptr);
      }
      if (F->isDeclaration()) {
        F->setLinkage(GlobalValue::ExternalWeakLinkage);
        F->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
        F->setDSOLocal(false);
#endif
      }
      if (WeakModule) {
        F = Function::Create(F->getFunctionType(), GlobalValue::WeakAnyLinkage,
#if LLVM_VERSION_MAJOR >= 8
                             F->getAddressSpace(),
#endif
                             F->getName(), WeakModule.get());
        BasicBlock *BB = BasicBlock::Create(F->getContext(), "", F);
        IRBuilder<> Builder(BB);
        Builder.CreateCall(WeakDefCalled,
                           {Builder.CreateGlobalStringPtr(GO.getName())});
        Builder.CreateUnreachable();
      }
    }
  }

  if (NoUnmuxedDefs) {
    for (GlobalObject &GO : M->global_objects()) {
      if (!GO.isDeclaration() && GO.isInterposable()) {
        GO.setLinkage(GO.hasLinkOnceLinkage() ? GlobalValue::LinkOnceODRLinkage
                                              : GlobalValue::WeakODRLinkage);
      }
    }
  }

  if (NoUnmuxedUsers) {
    StringSet<> MustExport;
    MustExport.insert("llvm.used");
    MustExport.insert("llvm.compiler.used");
    MustExport.insert("llvm.global_ctors");
    MustExport.insert("llvm.global_dtors");
    MustExport.insert("main");
    MustExport.insert("__bcdb_unreachable_function_called");
    MustExport.insert("__bcdb_weak_definition_called");
    for (auto &Item : StubModules) {
      Module &StubModule = *Item.second;
      for (GlobalObject &GO : StubModule.global_objects())
        if (GO.isDeclarationForLinker())
          MustExport.insert(GO.getName());
    }
    for (GlobalObject &GO : M->global_objects()) {
      if (!GO.isDeclarationForLinker() && !GO.hasLocalLinkage() &&
          !MustExport.count(GO.getName())) {
        GO.setLinkage(GlobalValue::InternalLinkage);
      }
    }
  }

  DiagnoseUnreachableFunctions(*M, UndefFuncType);
  for (auto &Item : StubModules) {
    Module &StubModule = *Item.second;
    DiagnoseUnreachableFunctions(StubModule, UndefFuncType);
  }

  return M;
}

std::unique_ptr<llvm::Module>
BCDB::Mux2(std::vector<llvm::StringRef> Names,
           llvm::StringMap<std::unique_ptr<llvm::Module>> &Stubs,
           std::unique_ptr<llvm::Module> *WeakModule) {
  Mux2Merger Merger(*this, WeakModule != nullptr);
  for (StringRef Name : Names) {
    Merger.AddModule(Name);
  }
  Merger.PrepareToRename();
  Merger.RenameEverything();
  auto Result = Merger.Finish();

  if (WeakModule)
    *WeakModule = std::move(Merger.WeakModule);
  Stubs = std::move(Merger.StubModules);
  return Result;
}
