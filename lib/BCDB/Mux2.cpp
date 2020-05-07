#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SpecialCaseList.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <map>
#include <set>

#include "Merge.h"
#include "Util.h"
#include "bcdb/LLVMCompat.h"

namespace {
#include "mux2_default_symbol_list.inc"
#include "mux2_library.inc"
} // namespace

using namespace bcdb;
using namespace llvm;

static cl::list<std::string> SpecialCaseFilename(
    "mux-symbol-list",
    cl::desc("Special case symbol list (sanitizer blacklist format)"),
    cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<bool> AllowSpuriousExports(
    "allow-spurious-exports",
    cl::desc("Allow the muxed module to export extra symbols that the original "
             "modules didn't export"),
    cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<bool> KnownDynamicDefs(
    "known-dynamic-defs",
    cl::desc("All dynamic definitions are listed with --mux-symbol-list"),
    cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<bool> KnownDynamicUses(
    "known-dynamic-uses",
    cl::desc("All dynamic uses are listed with --mux-symbol-list"),
    cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<bool> KnownRTLDLocal(
    "known-rtld-local",
    cl::desc("All RTLD_LOCAL declarations are listed with --mux-symbol-list"),
    cl::cat(MergeCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<bool>
    TrapUnreachableFunctions("trap-unreachable-functions",
                             cl::desc("Print an error message at run time if "
                                      "an unreachable function is called"),
                             cl::cat(MergeCategory),
                             cl::sub(*cl::AllSubCommands));

static std::unique_ptr<Module> LoadMuxLibrary(LLVMContext &Context) {
  ExitOnError Err("LoadMuxLibrary: ");
  StringRef Buffer(reinterpret_cast<char *>(mux2_library_bc),
                   mux2_library_bc_len);
  auto MainMod =
      Err(parseBitcodeFile(MemoryBufferRef(Buffer, "main"), Context));
  MainMod->setTargetTriple("");
  return MainMod;
}

static std::unique_ptr<MemoryBuffer> LoadDefaultSymbolList() {
  StringRef Buffer(reinterpret_cast<char *>(mux2_default_symbol_list_txt),
                   mux2_default_symbol_list_txt_len);
  return MemoryBuffer::getMemBuffer(Buffer, "mux2_default_symbol_list.txt");
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
  GlobalValue *LoadPartDefinition(GlobalItem &GI, Module *M = nullptr) override;
  void LoadRemainder(std::unique_ptr<Module> M,
                     std::vector<GlobalItem *> &GIs) override;

private:
  bool symbolInSection(StringRef Section, StringRef ModuleName, StringRef Name,
                       StringRef Category = StringRef());
  bool symbolInSection(StringRef Section, GlobalItem &GI,
                       StringRef Category = StringRef());
  bool mayBeDefinedDynamically(StringRef ModuleName, StringRef Name);
  bool mayBeUsedDynamically(StringRef ModuleName, StringRef Name);
  bool mayBeRTLDLocal(StringRef ModuleName, StringRef Name);
  void MakeAvailableExternally(GlobalValue *GV);

  std::unique_ptr<SpecialCaseList> DefaultSymbolList, SymbolList;
  StringMap<GlobalItem *> GlobalDefinitions;
  std::set<GlobalItem *> DirectlyReferenced;
};

Mux2Merger::Mux2Merger(BCDB &bcdb, bool enable_weak_module)
    : Merger(bcdb), SymbolList(createSpecialCaseList(SpecialCaseFilename)) {
  std::string Error;
  DefaultSymbolList =
      SpecialCaseList::create(LoadDefaultSymbolList().get(), Error);
  if (!Error.empty())
    report_fatal_error(Error);

  EnableMustTail = true;
  EnableNameReuse = false;

  if (enable_weak_module)
    WeakModule = std::make_unique<Module>("weak", bcdb.GetContext());

  MergedModule->setPICLevel(PICLevel::BigPIC);
  MergedModule->addModuleFlag(Module::Warning, "bcdb.elf.type", ELF::ET_DYN);
}

bool Mux2Merger::symbolInSection(StringRef Section, StringRef ModuleName,
                                 StringRef Name, StringRef Category) {
  for (SpecialCaseList *SCL : {DefaultSymbolList.get(), SymbolList.get()}) {
    if (SCL->inSection(Section, "fun", Name))
      return true;
    if (SCL->inSection(Section, "global", Name))
      return true;
    if (SCL->inSection(Section, "lib", ModuleName))
      return true;
  }
  return false;
}

bool Mux2Merger::symbolInSection(StringRef Section, GlobalItem &GI,
                                 StringRef Category) {
  return symbolInSection(Section, GI.ModuleName, GI.Name, Category);
}

bool Mux2Merger::mayBeDefinedDynamically(StringRef ModuleName, StringRef Name) {
  if (!KnownDynamicDefs)
    return true;
  if (symbolInSection("mux-dynamic-defs", ModuleName, Name))
    return true;
  if (symbolInSection("mux-always-defined-elsewhere", ModuleName, Name))
    return true;
  return false;
}

bool Mux2Merger::mayBeUsedDynamically(StringRef ModuleName, StringRef Name) {
  if (!KnownDynamicUses)
    return true;
  if (symbolInSection("mux-dynamic-uses", ModuleName, Name))
    return true;
  return false;
}

bool Mux2Merger::mayBeRTLDLocal(StringRef ModuleName, StringRef Name) {
  if (symbolInSection("mux-not-rtld-local", ModuleName, Name))
    return false;
  if (!KnownRTLDLocal)
    return true;
  return symbolInSection("mux-rtld-local", ModuleName, Name);
}

void Mux2Merger::PrepareToRename() {
  // Make stub modules.
  for (auto &Item : ModRemainders) {
    Item.second->setModuleIdentifier(Item.first());
    std::unique_ptr<Module> M = CloneModuleCorrectly(*Item.second);
    // Make all definitions external by default, so LoadPartDefinition will
    // work correctly. That will be changed in LoadRemainder if necessary.
    for (GlobalValue &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      if (!GV.isDeclarationForLinker())
        GV.setLinkage(GlobalValue::ExternalLinkage);
    }
    StubModules[Item.first()] = std::move(M);
  }

  // Find symbols that only have one definition.
  StringMap<int> ExportedCount;
  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;
    if (!GV->hasLocalLinkage()) {
      ExportedCount[GI.Name]++;
      GlobalDefinitions[GI.Name] = &GI;
    }
  }
  for (auto &Item : ExportedCount)
    if (Item.second > 1)
      GlobalDefinitions.erase(Item.first());
  // If the symbol may be defined elsewhere, we can't be sure whether to use
  // our definition or the external one.
  for (auto &Item : GlobalItems) {
    GlobalItem &GI = Item.second;
    if (mayBeDefinedDynamically(GI.ModuleName, GI.Name))
      GlobalDefinitions.erase(GI.Name);
  }

  // Find GIs that have some reference directly resolved to them.
  for (auto &Item : GlobalItems) {
    GlobalItem &GI = Item.second;
    for (auto &Ref : GI.Refs) {
      auto Res = Resolve(GI.ModuleName, Ref.first);
      if (Res.GI) {
        DirectlyReferenced.insert(Res.GI);
      } else {
        // The indirect reference prevents us from putting a definition of
        // Res.Name in the muxed module, just as if we had multiple definitions
        // of it.
        ExportedCount[Res.Name] = 2;
      }
    }
  }

  // Determine which GIs should be defined in the merged module.
  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;

    if (symbolInSection("mux-unmovable", GI)) {
      GI.DefineInMergedModule = false;
    } else if (GV->hasLocalLinkage()) {
      GI.DefineInMergedModule = true;
    } else if (ExportedCount[GI.Name] == 1) {
      if (AllowSpuriousExports)
        GI.DefineInMergedModule = true;
      else if (!mayBeUsedDynamically(GI.ModuleName, GI.Name))
        GI.DefineInMergedModule = true;
      else
        GI.DefineInMergedModule = false;
    } else {
      GI.DefineInMergedModule = false;
    }
  }

  // Some declarations can only be resolved correctly from the stub module.
  // If the GI refers to such a declaration, keep it in the stub module.
  for (auto &Item : GlobalItems) {
    GlobalItem &GI = Item.second;
    for (auto &Ref : GI.Refs) {
      if (!mayBeRTLDLocal(GI.ModuleName, Ref.first))
        continue;
      // If we can statically resolve the reference, we don't need to worry
      // about RTLD_LOCAL.
      auto Res = Resolve(GI.ModuleName, Ref.first);
      if (Res.GI)
        continue;
      // Move the definition to the stub module.
      GI.DefineInMergedModule = false;
      GI.BodyInStubModule = true;
      GlobalValue *GV = ModRemainders[GI.ModuleName]->getNamedValue(Ref.first);
      if (GV && !GV->isDeclaration())
        GlobalItems[GV].DefineInMergedModule = false;
    }
  }

  // Some global references must stay within the same module (an alias to an
  // aliasee, or a global constant to a blockaddress). Ensure that if either
  // part is put in the stub module, the other part is too.
  while (true) {
    bool Changed = false;
    for (auto &Item : GlobalItems) {
      GlobalValue *GV = Item.first;
      GlobalItem &GI = Item.second;
      SmallPtrSet<GlobalValue *, 8> ForcedSameModule;
      FindGlobalReferences(GV, &ForcedSameModule);
      for (GlobalValue *TargetGV : ForcedSameModule) {
        GlobalItem &Target = GlobalItems[TargetGV];
        if (GI.DefineInMergedModule != Target.DefineInMergedModule) {
          Target.DefineInMergedModule = false;
          GI.DefineInMergedModule = false;
          Changed = true;
        }
      }
    }
    if (!Changed)
      break;
  }

  // Find GIs that are directly referenced from the merged module or the stub
  // module.
  for (auto &Item : GlobalItems) {
    GlobalItem &GI = Item.second;
    bool RefFromMerged =
        GI.DefineInMergedModule || (!GI.PartID.empty() && !GI.BodyInStubModule);
    for (auto &Ref : GI.Refs) {
      auto Res = Resolve(GI.ModuleName, Ref.first);
      if (Res.GI && RefFromMerged)
        Res.GI->NeededInMergedModule = true;
      if (Res.GI && !RefFromMerged)
        Res.GI->NeededInStubModule = true;
    }
  }

  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;

    GI.AvailableExternallyInMergedModule = false;
    if (!GI.DefineInMergedModule && GI.NeededInMergedModule) {
      GI.AvailableExternallyInMergedModule = true;
      // Not only is available_externally pointless for a non-constant
      // variable, the __bcdb_direct_ alias also works incorrectly. If
      // @__bcdb_direct_foo is an alias to @foo in the library, the program may
      // redefine @foo in its own address space, but @__bcdb_direct_foo will
      // still point to the library's address space.
      if (GlobalVariable *Var = dyn_cast<GlobalVariable>(GV))
        if (!Var->isConstant())
          GI.AvailableExternallyInMergedModule = false;
    }

    if (GV->hasLocalLinkage() && GI.DefineInMergedModule &&
        GI.NeededInStubModule) {
      // The definition will go in the merged module. But the stub module may
      // need to import it, e.g., if it includes a global variable that points
      // to the private symbol. Rename the private global so we can safely
      // export it.
      GI.NewName = ReserveName("__bcdb_private_" + GI.Name);
    } else if (GV->hasLocalLinkage() && !GI.DefineInMergedModule &&
               GI.NeededInMergedModule) {
      GI.NewName = ReserveName("__bcdb_private_" + GI.Name);
    } else if (GI.AvailableExternallyInMergedModule &&
               ExportedCount[GI.Name] > 1) {
      // Add an alias, so we can make an available_externally copy for this
      // specific definition.
      GI.NewName = ReserveName("__bcdb_direct_" + GI.Name);
    } else if (!GV->hasLocalLinkage()) {
      // Keep the existing name.
      GI.NewName = GI.Name;
      ReservedNames.insert(GI.NewName);
    } else {
      // We don't care what the new name is! Merger::RenameEverything() will
      // handle it.
    }
  }
}

ResolvedReference Mux2Merger::Resolve(StringRef ModuleName, StringRef Name) {
  GlobalValue *GV = ModRemainders[ModuleName]->getNamedValue(Name);
  if (GV && GV->hasExactDefinition()) {
    assert(GlobalItems.count(GV));
    return ResolvedReference(&GlobalItems[GV]);
  } else if (GlobalDefinitions.count(Name)) {
    return ResolvedReference(GlobalDefinitions[Name]);
  } else {
    assert(!Name.empty());
    return ResolvedReference(Name);
  }
}

void Mux2Merger::MakeAvailableExternally(GlobalValue *GV) {
  LinkageMap.erase(GV);
  if (GlobalObject *GO = dyn_cast<GlobalObject>(GV)) {
    GV->setLinkage(GlobalValue::AvailableExternallyLinkage);
    GO->setComdat(nullptr);
    GV->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
    GV->setDSOLocal(false);
#endif
  } else if (isa<GlobalAlias>(GV)) {
    GV->setLinkage(GlobalValue::InternalLinkage);
    GV->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
    GV->setDSOLocal(true);
#endif
  }
}

static GlobalObject *ReplaceWithDeclaration(GlobalValue *GV) {
  Type *Type = GV->getValueType();
  GlobalObject *Decl;
  if (FunctionType *FType = dyn_cast<FunctionType>(Type)) {
    Decl = Function::Create(FType, GlobalValue::ExternalLinkage, "",
                            GV->getParent());
  } else {
    GlobalVariable *Base;
    if (GlobalAlias *GA = dyn_cast<GlobalAlias>(GV))
      Base = cast<GlobalVariable>(GA->getBaseObject());
    else
      Base = cast<GlobalVariable>(GV);
    Decl = new GlobalVariable(*GV->getParent(), Type, Base->isConstant(),
                              GlobalValue::ExternalLinkage, nullptr);
    Decl->setThreadLocalMode(Base->getThreadLocalMode());
  }
  std::string Name = GV->getName();
  GV->replaceAllUsesWith(Decl);
  GV->eraseFromParent();
  Decl->setName(Name);
  return Decl;
}

GlobalValue *Mux2Merger::LoadPartDefinition(GlobalItem &GI, Module *M) {
  if (!GI.DefineInMergedModule && GI.BodyInStubModule) {
    GlobalValue *Result =
        Merger::LoadPartDefinition(GI, StubModules[GI.ModuleName].get());
    return Result;
  } else {
    return Merger::LoadPartDefinition(GI, M);
  }
}

void Mux2Merger::AddPartStub(Module &MergedModule, GlobalItem &GI,
                             GlobalValue *Def, GlobalValue *Decl,
                             StringRef NewName) {
  if (NewName.empty())
    NewName = GI.NewName;
  Module &StubModule = *StubModules[GI.ModuleName];

  if (GI.DefineInMergedModule) {
    Merger::AddPartStub(MergedModule, GI, Def, Decl, NewName);

    if (GI.NeededInStubModule) {
      // Export the symbol from the merged module.
      GlobalValue *NewStub = MergedModule.getNamedValue(NewName);
      LinkageMap[NewStub] = GlobalValue::ExternalLinkage;
      NewStub->setLinkage(GlobalValue::ExternalLinkage);
      NewStub->setVisibility(GlobalValue::ProtectedVisibility);

      // Import the symbol into the stub module.
      GlobalValue *StubInStubModule = StubModule.getNamedValue(GI.Name);
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
    if (!GI.BodyInStubModule) {
      // Export the body from the merged module.
      LinkageMap[Def] = GlobalValue::ExternalLinkage;
      Def->setLinkage(GlobalValue::ExternalLinkage);
      Def->setVisibility(GlobalValue::ProtectedVisibility);
    }

    // Import the body into the stub module.
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
    GlobalValue *StubStub = StubModule.getNamedValue(GI.Name);

    if (GlobalValue::isLocalLinkage(LinkageMap[StubStub]) &&
        GI.NeededInMergedModule) {
      LinkageMap.erase(StubStub);
      StubStub->setName(GI.NewName);
      StubStub->setLinkage(GlobalValue::ExternalLinkage);
      StubStub->setVisibility(GlobalValue::ProtectedVisibility);
    } else if (GI.Name != GI.NewName) {
      // If we have an alternate NewName, we need an alias.
      GlobalAlias::create(GlobalValue::ExternalLinkage, GI.NewName, StubStub);
    }

    if (GI.AvailableExternallyInMergedModule && !GI.BodyInStubModule) {
      // Add an available_externally definition to the merged module.
      Merger::AddPartStub(MergedModule, GI, Def, Decl, GI.NewName);
      MakeAvailableExternally(MergedModule.getNamedValue(NewName));
    }
  }
}

void Mux2Merger::LoadRemainder(std::unique_ptr<Module> M,
                               std::vector<GlobalItem *> &GIs) {
  Module &StubModule = *StubModules[M->getModuleIdentifier()];
  std::vector<GlobalItem *> GIsToMerge;

  // Make everything internal by default, unless we actually need it.
  for (GlobalValue &GV :
       concat<GlobalValue>(StubModule.global_objects(), StubModule.aliases(),
                           StubModule.ifuncs())) {
    if (!GV.isDeclarationForLinker() && !LinkageMap.count(&GV) &&
        !GV.getName().startswith("__bcdb_private"))
      GV.setLinkage(GlobalValue::InternalLinkage);
  }

  for (GlobalItem *GI : GIs) {
    if (GI->DefineInMergedModule) {

      // Define in the merged module.
      GIsToMerge.push_back(GI);
      if (GI->NeededInStubModule) {
        // Define private globals in the merged module, but export them so the
        // stub module can use them.
        GlobalValue *GV = M->getNamedValue(GI->NewName);
        GV->setLinkage(GlobalValue::ExternalLinkage);
        GV->setVisibility(GlobalValue::ProtectedVisibility);
      }

      // Make the stub module's version available_externally.
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      NewGV->setName(GI->NewName);
      if (!NewGV->isDeclaration())
        MakeAvailableExternally(NewGV);

      // If it's an alias or uses blockaddresses, replace it with a declaration
      // in the stub module.
      SmallPtrSet<GlobalValue *, 8> ForcedSameModule;
      FindGlobalReferences(NewGV, &ForcedSameModule);
      if (!ForcedSameModule.empty())
        NewGV = ReplaceWithDeclaration(NewGV);

    } else {
      // Export the definition from the stub module.
      GlobalValue *GV = M->getNamedValue(GI->NewName);
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      NewGV->setLinkage(GV->getLinkage());
#if LLVM_VERSION_MAJOR >= 7
      NewGV->setDSOLocal(GV->isDSOLocal());
#endif

      if (NewGV->hasLocalLinkage() && GI->NeededInMergedModule) {
        NewGV->setName(GI->NewName);
        NewGV->setLinkage(GlobalValue::ExternalLinkage);
        NewGV->setVisibility(GlobalValue::ProtectedVisibility);
      } else if (GI->Name != GI->NewName) {
        // If we have an alternate NewName, we need an alias.
        GlobalAlias::create(GlobalValue::ExternalLinkage, GI->NewName, NewGV);
      }

      if (GI->AvailableExternallyInMergedModule && isa<GlobalObject>(GV)) {
        // Add an available_externally definition to the merged module.
        // TODO: handle aliases too, but only if the aliasee is defined.
        GIsToMerge.push_back(GI);
        MakeAvailableExternally(GV);
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
  Merger::LoadRemainder(std::move(M), GIsToMerge);
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

  // Run some optimizations to make use of the available_externally functions
  // we created.
  legacy::PassManager PM;
  PM.add(createInstructionCombiningPass(/*ExpensiveCombines*/ false));
  PM.add(createConstantPropagationPass());
  PM.add(createAlwaysInlinerLegacyPass());
  PM.add(createGlobalDCEPass());
  PM.run(*M);

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
                             StubModule.ifuncs())) {
      if (GV.hasLinkOnceLinkage()) {
        GlobalValue *Used = M->getNamedValue(GV.getName());
        if (Used && !Used->use_empty() && !Used->hasExactDefinition())
          GV.setLinkage(
              GlobalValue::getWeakLinkage(GV.hasLinkOnceODRLinkage()));
      }
    }

    // Remove anything we didn't decide to export.
    createGlobalDCEPass()->runOnModule(StubModule);
  }

  // Make weak definitions for everything declared in the merged module. That
  // way we can link against the merged library even if we're not linking
  // against any particular stub library.
  for (GlobalObject &GO : M->global_objects()) {
    if (!GO.isDeclarationForLinker())
      continue;

    if (symbolInSection("mux-always-defined-elsewhere", "", GO.getName()))
      continue;

    if (GlobalVariable *Var = dyn_cast<GlobalVariable>(&GO)) {
      Var->setInitializer(nullptr);
      Var->setComdat(nullptr);
      Var->setLinkage(GlobalValue::ExternalWeakLinkage);
      Var->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
      Var->setDSOLocal(false);
#endif
    } else if (Function *F = dyn_cast<Function>(&GO)) {
      F->deleteBody();
      F->setComdat(nullptr);
      F->setLinkage(GlobalValue::ExternalWeakLinkage);
      F->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
      F->setDSOLocal(false);
#endif
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

  StringSet<> MustExport;
  for (auto &Item : StubModules) {
    Module &StubModule = *Item.second;
    for (GlobalObject &GO : StubModule.global_objects())
      if (GO.isDeclarationForLinker())
        MustExport.insert(GO.getName());
  }

  for (auto &Item : GlobalItems) {
    GlobalItem &GI = Item.second;
    if (!GI.DefineInMergedModule)
      continue;
    GlobalValue *GV = M->getNamedValue(GI.NewName);
    if (!GV)
      continue; // globals can be removed by globaldce, above
    assert(!GV->isDeclarationForLinker());
    if (GlobalObject *GO = dyn_cast<GlobalObject>(GV)) {
      // If we know there's only one possible definition, use a
      // non-interposable linkage and a protected visibility.
      if (!mayBeDefinedDynamically(GI.ModuleName, GI.NewName)) {
        if (!GO->isDefinitionExact())
          GO->setLinkage(GlobalValue::ExternalLinkage);
        if (!GO->hasLocalLinkage() && GO->hasDefaultVisibility())
          GO->setVisibility(GlobalValue::ProtectedVisibility);
      }

      // If we know there are no users outside the merged module, internalize
      // it.
      if (!mayBeUsedDynamically(GI.ModuleName, GI.NewName) &&
          !MustExport.count(GI.NewName)) {
        GO->setLinkage(GlobalValue::InternalLinkage);
      }
    }
  }

  if (TrapUnreachableFunctions) {
    DiagnoseUnreachableFunctions(*M, UndefFuncType);
    for (auto &Item : StubModules) {
      Module &StubModule = *Item.second;
      DiagnoseUnreachableFunctions(StubModule, UndefFuncType);
    }
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
