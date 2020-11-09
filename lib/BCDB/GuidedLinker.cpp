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
#include <llvm/IR/NoFolder.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SpecialCaseList.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/IPO/FunctionImport.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <map>
#include <set>

#include "Merge.h"
#include "Util.h"
#include "bcdb/LLVMCompat.h"

namespace {
#include "gl_default_symbol_list.inc"
#include "gl_library.inc"
} // namespace

using namespace bcdb;
using namespace llvm;

static cl::list<std::string> SpecialCaseFilename(
    "gl-symbol-list",
    cl::desc("Special case symbol list (sanitizer blacklist format)"),
    cl::cat(MergeCategory));

static cl::opt<bool>
    AllowSpuriousExports("noweak",
                         cl::desc("Apply NoWeak constraint by default"),
                         cl::cat(MergeCategory));

static cl::opt<bool>
    KnownDynamicDefs("nooverride",
                     cl::desc("Apply NoOverride constraint by default"),
                     cl::cat(MergeCategory));

static cl::opt<bool>
    KnownDynamicUses("nouse", cl::desc("Apply NoUse constraint by default"),
                     cl::cat(MergeCategory));

static cl::opt<bool>
    KnownRTLDLocal("noplugin", cl::desc("Apply NoPlugin constraint by default"),
                   cl::cat(MergeCategory));

static cl::opt<bool>
    TrapUnreachableFunctions("trap-unreachable-functions",
                             cl::desc("Print an error message at run time if "
                                      "an unreachable function is called"),
                             cl::cat(MergeCategory));

static cl::opt<bool>
    DisableOpts("disable-opts",
                cl::desc("Disable optimizations that use available_externally"),
                cl::cat(MergeCategory));

static cl::opt<bool> Debug("debug-gl",
                           cl::desc("Debugging output for guided linker"),
                           cl::cat(MergeCategory));

static cl::opt<bool>
    DisableDSOLocal("disable-dso-local",
                    cl::desc("Disable protected visibility and dso_local"),
                    cl::cat(MergeCategory));

static std::unique_ptr<Module> LoadMuxLibrary(LLVMContext &Context) {
  ExitOnError Err("LoadMuxLibrary: ");
  StringRef Buffer(reinterpret_cast<char *>(gl_library_bc), gl_library_bc_len);
  auto MainMod =
      Err(parseBitcodeFile(MemoryBufferRef(Buffer, "main"), Context));
  MainMod->setTargetTriple("");
  return MainMod;
}

static std::unique_ptr<MemoryBuffer> LoadDefaultSymbolList() {
  StringRef Buffer(reinterpret_cast<char *>(gl_default_symbol_list_txt),
                   gl_default_symbol_list_txt_len);
  return MemoryBuffer::getMemBuffer(Buffer, "gl_default_symbol_list.txt");
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
  void FixupPartDefinition(GlobalItem &GI, Function &Body) override;

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
  StringMap<StringMap<GlobalVariable *>> RTLDLocalImportVariables;
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
  if (symbolInSection("gl-override", ModuleName, Name))
    return true;
  if (symbolInSection("gl-always-defined-externally", ModuleName, Name))
    return true;
  return false;
}

bool Mux2Merger::mayBeUsedDynamically(StringRef ModuleName, StringRef Name) {
  if (!KnownDynamicUses)
    return true;
  if (symbolInSection("gl-use", ModuleName, Name))
    return true;
  return false;
}

bool Mux2Merger::mayBeRTLDLocal(StringRef ModuleName, StringRef Name) {
  if (symbolInSection("gl-noplugin", ModuleName, Name))
    return false;
  if (symbolInSection("gl-always-defined-externally", ModuleName, Name))
    return false;
  if (!KnownRTLDLocal)
    return true;
  return symbolInSection("gl-plugin", ModuleName, Name);
}

void Mux2Merger::PrepareToRename() {
  // Make stub modules.
  for (auto &Item : ModRemainders) {
    Item.second->setModuleIdentifier(Item.first());

    // In theory we could just call CloneModuleCorrectly(*Item.second) to get
    // the stub module. But it seems like that might cause problems with
    // IRMover and type completion, because CloneModuleCorrectly doesn't create
    // copies of opaque types:
    // https://lists.llvm.org/pipermail/llvm-dev/2018-March/122151.html
    ExitOnError Err("Mux2Merger::PrepareToRename: ");
    std::map<std::string, std::string> PartIDs;
    std::unique_ptr<Module> M = Err(bcdb.LoadParts(Item.first(), PartIDs));
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
  // If the symbol may be defined externally, we can't be sure whether to use
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

    if (symbolInSection("gl-unmovable", GI)) {
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

  // Some global references must stay within the same module (an alias to an
  // aliasee, or a global constant to a blockaddress). Ensure that if either
  // part is put in the stub module, the other part is too.
  while (true) {
    bool Changed = false;
    for (auto &Item : GlobalItems) {
      GlobalValue *GV = Item.first;
      GlobalItem &GI = Item.second;

      if (GI.RefersToRTLDLocal && GI.PartID.empty() &&
          GI.DefineInMergedModule) {
        GI.DefineInMergedModule = false;
        Changed = true;
      }

      SmallPtrSet<GlobalValue *, 8> ForcedSameModule;
      FindGlobalReferences(GV, &ForcedSameModule);
      for (GlobalValue *TargetGV : ForcedSameModule) {
        assert(GlobalItems.count(TargetGV));
        GlobalItem &Target = GlobalItems[TargetGV];
        if (GI.DefineInMergedModule != Target.DefineInMergedModule) {
          Target.DefineInMergedModule = false;
          GI.DefineInMergedModule = false;
          Changed = true;
        }
      }

      // Some declarations can only be resolved correctly from the stub
      // module. Check whether the GI refers to such a declaration.
      for (auto &Ref : GI.Refs) {
        auto Res = Resolve(GI.ModuleName, Ref.first);
        if (Res.GI && Res.GI->DefineInMergedModule)
          continue;
        if (!mayBeRTLDLocal(GI.ModuleName, Ref.first))
          continue;
        if (!GI.RefersToRTLDLocal) {
          Changed = true;
          GI.RefersToRTLDLocal = true;
        }
        if (!GI.PartID.empty()) {
          if (!RTLDLocalImportVariables[GI.ModuleName].count(Ref.first)) {
            Changed = true;
            GlobalValue *Decl =
                StubModules[GI.ModuleName]->getNamedValue(Ref.first);
            StringRef ModuleShortName =
                StringRef(GI.ModuleName).rsplit('/').second;
            if (ModuleShortName.empty())
              ModuleShortName = GI.ModuleName;
            std::string Name = ReserveName(
                ("__bcdb_import_" + Ref.first + "_" + ModuleShortName).str());
            GlobalVariable *MergedVar = new GlobalVariable(
                *MergedModule, Decl->getType(), false,
                GlobalValue::ExternalLinkage,
                Constant::getNullValue(Decl->getType()), Name);
            assert(MergedVar->getName() == Name);
            RTLDLocalImportVariables[GI.ModuleName][Ref.first] = MergedVar;
          }
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
    GI.AvailableExternallyInStubModule = true;
    bool RefFromMerged =
        GI.DefineInMergedModule || (!GI.PartID.empty() && !GI.BodyInStubModule);
    for (auto &Ref : GI.Refs) {
      auto Res = Resolve(GI.ModuleName, Ref.first);
      if (RefFromMerged && GI.RefersToRTLDLocal)
        if (RTLDLocalImportVariables[GI.ModuleName].count(Ref.first))
          continue;
      if (Res.GI && RefFromMerged)
        Res.GI->NeededInMergedModule = true;
      if (Res.GI && !RefFromMerged)
        Res.GI->NeededInStubModule = true;
    }
  }

  while (true) {
    bool Changed = false;
    for (auto &Item : GlobalItems) {
      GlobalValue *GV = Item.first;
      GlobalItem &GI = Item.second;
      if (!GI.AvailableExternallyInStubModule)
        continue;
      if (GV->hasLocalLinkage() && GI.DefineInMergedModule &&
          !GI.NeededInStubModule) {
        GI.AvailableExternallyInStubModule = false;
        Changed = true;
      }
      SmallPtrSet<GlobalValue *, 8> Refs, ForcedSameModule;
      Refs = FindGlobalReferences(GV, &ForcedSameModule);
      if (!ForcedSameModule.empty()) {
        GI.AvailableExternallyInStubModule = false;
        Changed = true;
        continue;
      }
      for (GlobalValue *TargetGV : Refs) {
        if (GlobalItems.count(TargetGV)) {
          GlobalItem &Target = GlobalItems[TargetGV];
          if (!Target.AvailableExternallyInStubModule) {
            GI.AvailableExternallyInStubModule = false;
            Changed = true;
            break;
          }
        }
      }
    }
    if (!Changed)
      break;
  }

  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;

    GI.AvailableExternallyInMergedModule = false;
    if (!GI.DefineInMergedModule && GI.NeededInMergedModule &&
        !GI.BodyInStubModule) {
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
      GI.NewName = ReserveName("__bcdb_merged_" + GI.Name);
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

    if (Debug) {
      errs() << GI.ModuleName << " " << GI.Name << "\n";
      errs() << "  define in " << (GI.DefineInMergedModule ? "merged" : "stub")
             << "\n";
      errs() << "  body in " << (GI.BodyInStubModule ? "stub" : "merged")
             << "\n";
      if (GV->hasLocalLinkage())
        errs() << "  local\n";
      if (GI.NeededInStubModule)
        errs() << "  needed in stub\n";
      if (GI.NeededInMergedModule)
        errs() << "  needed in merged\n";
      if (GI.AvailableExternallyInMergedModule)
        errs() << "  available externally in merged module\n";
      if (GI.AvailableExternallyInStubModule)
        errs() << "  available externally in stub module\n";
      errs() << "  export count: " << ExportedCount[GI.Name] << "\n";
      errs() << "  new name: " << GI.NewName << "\n";
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

static void expandConstant(Constant *C, Function *F) {
  // Based on:
  // https://chromium.googlesource.com/native_client/pnacl-llvm/+/mseaborn/merge-34-squashed/lib/Transforms/NaCl/ExpandTlsConstantExpr.cpp
  // but with support for ConstantAggregate.
  for (Value::use_iterator UI = C->use_begin(); UI != C->use_end(); ++UI)
    if (Constant *User = dyn_cast<Constant>(UI->getUser()))
      expandConstant(User, F);
  C->removeDeadConstantUsers();
  if (C->use_empty())
    return;

  IRBuilder<NoFolder> Builder(&F->getEntryBlock().front());
  Value *NewInst;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
    NewInst = Builder.Insert(CE->getAsInstruction());
  } else if (ConstantAggregate *CA = dyn_cast<ConstantAggregate>(C)) {
    NewInst = UndefValue::get(CA->getType());
    for (unsigned I = 0, E = CA->getNumOperands(); I != E; ++I)
      NewInst =
          isa<ConstantVector>(CA)
              ? Builder.CreateInsertElement(NewInst, CA->getOperand(I), I)
              : Builder.CreateInsertValue(NewInst, CA->getOperand(I), {I});
  } else {
    return;
  }
  C->replaceAllUsesWith(NewInst);
}

void Mux2Merger::FixupPartDefinition(GlobalItem &GI, Function &Body) {
  if (GI.BodyInStubModule || !GI.RefersToRTLDLocal)
    return;
  StringMap<GlobalVariable *> &ImportVars =
      RTLDLocalImportVariables[GI.ModuleName];
  for (GlobalObject &GO : Body.getParent()->global_objects()) {
    if (ImportVars.count(GO.getName())) {
      expandConstant(&GO, &Body);
      // It would probably work fine now to use GO.getType() here. There were
      // problems before when I was using CloneModule to create stub modules;
      // the cloned module would share the same types as the original module,
      // and when recursive structure types were involved, IRMover could get
      // screwed up.
      Type *T = Type::getInt8PtrTy(Body.getContext());
      GlobalVariable *Var = new GlobalVariable(
          *Body.getParent(), T, false, GlobalValue::ExternalLinkage, nullptr,
          ImportVars[GO.getName()]->getName());
      IRBuilder<> Builder(&Body.getEntryBlock().front());
      Value *Load = Builder.CreateLoad(Var);
      Load = Builder.CreatePointerBitCastOrAddrSpaceCast(Load, GO.getType());
      GO.replaceAllUsesWith(Load);
    }
  }
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
      ReplaceGlobal(StubModule, NewName, StubInStubModule);
      LinkageMap[StubInStubModule] = GlobalValue::ExternalLinkage;
      StubInStubModule->setLinkage(GlobalValue::ExternalLinkage);
      convertToDeclaration(*StubInStubModule);
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
      ReplaceGlobal(StubModule, GI.NewName, StubStub);
      StubStub->setLinkage(GlobalValue::ExternalLinkage);
      StubStub->setVisibility(GlobalValue::ProtectedVisibility);
    } else if (GI.Name != GI.NewName) {
      // If we have an alternate NewName, we need an alias.
      ReplaceGlobal(StubModule, GI.NewName,
                    GlobalAlias::create(GlobalValue::ExternalLinkage,
                                        GI.NewName, StubStub));
    }

    if (GI.AvailableExternallyInMergedModule) {
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
        !GV.getName().startswith("__bcdb_"))
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
        GV->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
        GV->setDSOLocal(false);
#endif
      }

      // Make the stub module's version available_externally.
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      ReplaceGlobal(StubModule, GI->NewName, NewGV);
      if (!NewGV->isDeclaration()) {
        if (GI->AvailableExternallyInStubModule) {
          assert(!M->getNamedValue(GI->NewName)->hasLocalLinkage());
          MakeAvailableExternally(NewGV);
        } else {
          if (!convertToDeclaration(*NewGV))
            NewGV = nullptr;
        }
      }

    } else {
      // Export the definition from the stub module.
      GlobalValue *GV = M->getNamedValue(GI->NewName);
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      LinkageMap.erase(NewGV);
      NewGV->setLinkage(GV->getLinkage());
#if LLVM_VERSION_MAJOR >= 7
      NewGV->setDSOLocal(GV->isDSOLocal());
#endif

      if (NewGV->hasLocalLinkage() && GI->NeededInMergedModule) {
        ReplaceGlobal(StubModule, GI->NewName, NewGV);
        NewGV->setLinkage(GlobalValue::ExternalLinkage);
        NewGV->setVisibility(GlobalValue::ProtectedVisibility);
      } else if (GI->Name != GI->NewName) {
        // If we have an alternate NewName, we need an alias.
        ReplaceGlobal(StubModule, GI->NewName,
                      GlobalAlias::create(GlobalValue::ExternalLinkage,
                                          GI->NewName, NewGV));
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

  if (!DisableOpts) {
    // Run some optimizations to make use of the available_externally functions
    // we created.
    legacy::PassManager PM;
    PM.add(createInstructionCombiningPass(/*ExpensiveCombines*/ false));
    PM.add(createConstantPropagationPass());
    PM.add(createAlwaysInlinerLegacyPass());
    PM.add(createGlobalDCEPass());
    PM.run(*M);
  }

  Linker::linkModules(*M, LoadMuxLibrary(M->getContext()));
  FunctionType *UndefFuncType =
      M->getFunction("__bcdb_unreachable_function_called")->getFunctionType();
  Function *WeakDefCalled;
  if (WeakModule)
    WeakDefCalled =
        Function::Create(UndefFuncType, GlobalValue::ExternalLinkage,
                         "__bcdb_weak_definition_called", WeakModule.get());

  for (auto &Item : StubModules) {
    StringRef ModuleName = Item.first();
    Module &StubModule = *Item.second;

    StringMap<GlobalVariable *> &ImportVars =
        RTLDLocalImportVariables[Item.first()];
    if (!ImportVars.empty()) {
      std::vector<Type *> Types;
      std::vector<Constant *> Values;
      std::vector<GlobalVariable *> Vars;
      for (auto &ImportVar : ImportVars) {
        StringRef Name = ImportVar.first();
        GlobalVariable *Var = ImportVar.second;
        Vars.push_back(Var);
        Types.push_back(Var->getValueType());
        Values.push_back(ConstantExpr::getPointerBitCastOrAddrSpaceCast(
            StubModule.getNamedValue(Name), Types.back()));
      }
      StructType *SType =
          StructType::create(Types, ("__bcdb_imports_" + ModuleName).str());
      PointerType *PType = SType->getPointerTo();

      Function *Callee = Function::Create(
          FunctionType::get(Type::getVoidTy(StubModule.getContext()), {PType},
                            false),
          GlobalValue::ExternalLinkage, "__bcdb_set_imports_" + ModuleName,
          M.get());

      {
        Function *Decl = Function::Create(Callee->getFunctionType(),
                                          GlobalValue::ExternalLinkage,
                                          Callee->getName(), &StubModule);
        Constant *Value = ConstantStruct::get(SType, Values);
        GlobalVariable *StubVar = new GlobalVariable(
            StubModule, SType, true, GlobalValue::ExternalLinkage, Value,
            ("__bcdb_imports_" + ModuleName).str());
        Function *F = Function::Create(
            FunctionType::get(Type::getVoidTy(StubModule.getContext()), false),
            GlobalValue::InternalLinkage, "__bcdb_init_imports", &StubModule);
        BasicBlock *BB = BasicBlock::Create(F->getContext(), "", F);
        IRBuilder<> Builder(BB);
        Builder.CreateCall(Decl, {StubVar});
        Builder.CreateRetVoid();
        appendToGlobalCtors(StubModule, F, 0);
      }

      BasicBlock *BB = BasicBlock::Create(Callee->getContext(), "", Callee);
      IRBuilder<> Builder(BB);
      for (size_t i = 0; i < Vars.size(); i++) {
        GlobalVariable *Var = Vars[i];
        Value *Ptr = Builder.CreateStructGEP(nullptr, Callee->arg_begin(), i);
        Value *Val = Builder.CreateLoad(Ptr);
        Builder.CreateStore(Val, Var);
        Var->setLinkage(GlobalValue::InternalLinkage);
      }
      Builder.CreateRetVoid();
    }

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

    if (symbolInSection("gl-always-defined-externally", "", GO.getName()))
      continue;

    if (GlobalVariable *Var = dyn_cast<GlobalVariable>(&GO)) {
      convertToDeclaration(*Var);
      Var->setLinkage(GlobalValue::ExternalWeakLinkage);
      Var->setVisibility(GlobalValue::DefaultVisibility);
#if LLVM_VERSION_MAJOR >= 7
      Var->setDSOLocal(false);
#endif
    } else if (Function *F = dyn_cast<Function>(&GO)) {
      convertToDeclaration(*F);
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
        if (!GO->hasLocalLinkage() && GO->hasDefaultVisibility() &&
            isa<Function>(GO))
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

  if (DisableDSOLocal) {
    for (GlobalValue &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      GV.setVisibility(GlobalValue::DefaultVisibility);
      if (!GV.hasLocalLinkage())
        GV.setDSOLocal(false);
    }
    for (auto &Item : StubModules) {
      Module &StubModule = *Item.second;
      for (GlobalValue &GV :
           concat<GlobalValue>(StubModule.global_objects(),
                               StubModule.aliases(), StubModule.ifuncs())) {
        GV.setVisibility(GlobalValue::DefaultVisibility);
        if (!GV.hasLocalLinkage())
          GV.setDSOLocal(false);
      }
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
