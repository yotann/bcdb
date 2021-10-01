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
#include <llvm/Support/VirtualFileSystem.h>
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
#include "bcdb/GlobalReferenceGraph.h"
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
    NoWeakDefault("noweak", cl::desc("Apply NoWeak constraint by default"),
                  cl::cat(MergeCategory));

static cl::opt<bool>
    NoOverrideDefault("nooverride",
                      cl::desc("Apply NoOverride constraint by default"),
                      cl::cat(MergeCategory));

static cl::opt<bool> NoUseDefault("nouse",
                                  cl::desc("Apply NoUse constraint by default"),
                                  cl::cat(MergeCategory));

static cl::opt<bool>
    NoPluginDefault("noplugin",
                    cl::desc("Apply NoPlugin constraint by default"),
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

static std::unique_ptr<Module> LoadGLLibrary(LLVMContext &Context) {
  ExitOnError Err("LoadGLLibrary: ");
  StringRef Buffer(reinterpret_cast<char *>(gl_library_bc), gl_library_bc_len);
  auto MainMod =
      Err(parseBitcodeFile(MemoryBufferRef(Buffer, "main"), Context));
  MainMod->setTargetTriple("");
  return MainMod;
}

static std::unique_ptr<MemoryBuffer> LoadDefaultSymbolList() {
  StringRef Buffer(reinterpret_cast<char *>(gl_default_symbol_list_txt),
                   gl_default_symbol_list_txt_len);
  return MemoryBuffer::getMemBuffer(Buffer, "gl_default_symbol_list.txt",
                                    /* RequiresNullTerminator */ false);
}

// Handling references from the merged library to the wrapper libraries:
// - The merged library will refer to various symbols that are defined in the
//   wrapper libraries, but for any given execution, only a subset of the
//   wrapper libraries will be loaded. So some of these references will be
//   undefined.
// - If we try just leaving these references undefined, both ld and ld.so will
//   error out.
//   - We can convince ld to let us do this by building the merged library with
//     "-z undefs" and building wrapper programs with "--allow-shlib-undefined".
//   - For functions, we can convince ld.so to allow this by running ld with
//     "-z lazy".
//   - But for globals, ld.so will always abort if it can't find a definition.
//   - Also, lazy functions don't work if the address of the function is used
//     in a global variable initializer.
// - Instead we can add weak definitions of everything to the merged library.
//   - We need to set LD_DYNAMIC_WEAK whenever we run an optimized program, to
//     ensure that the weak definition is overridden by any strong definitions.
//   - This still doesn't work right in some cases.
// - Instead we can add weak definitions of everything to a separate "weak
//   library", and ensure that that library is the last thing loaded by the
//   dynamic linker, so it's checked for symbols last.
// - See also ld's -Bsymbolic option.

class GLMerger : public Merger {
public:
  GLMerger(BCDB &bcdb, bool enable_weak_module);
  ResolvedReference Resolve(StringRef ModuleName, StringRef Name) override;
  void PrepareToRename();
  std::unique_ptr<Module> Finish();

  StringMap<std::unique_ptr<Module>> WrapperModules;
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
  bool mayHaveExternalOverrides(StringRef ModuleName, StringRef Name);
  bool mayHaveDynamicUses(StringRef ModuleName, StringRef Name);
  bool mayNeedPluginScope(StringRef ModuleName, StringRef Name);
  void MakeAvailableExternally(GlobalValue *GV);

  std::unique_ptr<SpecialCaseList> DefaultSymbolList, SymbolList;
  StringMap<GlobalItem *> GlobalDefinitions;
  std::set<GlobalItem *> DirectlyReferenced;
  StringMap<StringMap<GlobalVariable *>> PluginScopeImportVariables;
};

GLMerger::GLMerger(BCDB &bcdb, bool enable_weak_module)
    : Merger(bcdb), SymbolList(SpecialCaseList::createOrDie(
                        SpecialCaseFilename, *llvm::vfs::getRealFileSystem())) {
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

bool GLMerger::symbolInSection(StringRef Section, StringRef ModuleName,
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

bool GLMerger::symbolInSection(StringRef Section, GlobalItem &GI,
                               StringRef Category) {
  return symbolInSection(Section, GI.ModuleName, GI.Name, Category);
}

bool GLMerger::mayHaveExternalOverrides(StringRef ModuleName, StringRef Name) {
  if (!NoOverrideDefault)
    return true;
  if (symbolInSection("gl-override", ModuleName, Name))
    return true;
  if (symbolInSection("gl-always-defined-externally", ModuleName, Name))
    return true;
  return false;
}

bool GLMerger::mayHaveDynamicUses(StringRef ModuleName, StringRef Name) {
  if (!NoUseDefault)
    return true;
  if (symbolInSection("gl-use", ModuleName, Name))
    return true;
  return false;
}

bool GLMerger::mayNeedPluginScope(StringRef ModuleName, StringRef Name) {
  if (symbolInSection("gl-noplugin", ModuleName, Name))
    return false;
  if (symbolInSection("gl-always-defined-externally", ModuleName, Name))
    return false;
  if (!NoPluginDefault)
    return true;
  return symbolInSection("gl-plugin", ModuleName, Name);
}

void GLMerger::PrepareToRename() {
  // Make wrapper modules.
  for (auto &Item : ModRemainders) {
    Item.second->setModuleIdentifier(Item.first());

    // In theory we could just call CloneModule(*Item.second) to get the
    // wrapper module. But CloneModule can't handle blockaddresses in global
    // variable initializers, and it might also cause problems with IRMover and
    // type completion, because it doesn't create copies of opaque types:
    // https://lists.llvm.org/pipermail/llvm-dev/2018-March/122151.html
    ExitOnError Err("GLMerger::PrepareToRename: ");
    std::map<std::string, std::string> PartIDs;
    std::unique_ptr<Module> M = Err(bcdb.LoadParts(Item.first(), PartIDs));
    // Make all definitions external by default, so LoadPartDefinition will
    // work correctly. That will be changed in LoadRemainder if necessary.
    for (GlobalValue &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      if (!GV.isDeclarationForLinker())
        GV.setLinkage(GlobalValue::ExternalLinkage);
    }
    WrapperModules[Item.first()] = std::move(M);
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
    if (mayHaveExternalOverrides(GI.ModuleName, GI.Name))
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
        // Res.Name in the merged module, just as if we had multiple definitions
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
      if (NoWeakDefault)
        GI.DefineInMergedModule = true;
      else if (!mayHaveDynamicUses(GI.ModuleName, GI.Name))
        GI.DefineInMergedModule = true;
      else
        GI.DefineInMergedModule = false;
    } else {
      GI.DefineInMergedModule = false;
    }
  }

  // Some global references must stay within the same module (an alias to an
  // aliasee, or a global constant to a blockaddress). Ensure that if either
  // part is put in the wrapper module, the other part is too.
  while (true) {
    bool Changed = false;
    for (auto &Item : GlobalItems) {
      GlobalValue *GV = Item.first;
      GlobalItem &GI = Item.second;

      if (GI.RefersToPluginScope && GI.PartID.empty() &&
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

      // Some declarations can only be resolved correctly from the wrapper
      // module. Check whether the GI refers to such a declaration.
      for (auto &Ref : GI.Refs) {
        auto Res = Resolve(GI.ModuleName, Ref.first);
        if (Res.GI && Res.GI->DefineInMergedModule)
          continue;
        if (!mayNeedPluginScope(GI.ModuleName, Ref.first))
          continue;
        if (!GI.RefersToPluginScope) {
          Changed = true;
          GI.RefersToPluginScope = true;
        }
        if (!GI.PartID.empty()) {
          if (!PluginScopeImportVariables[GI.ModuleName].count(Ref.first)) {
            Changed = true;
            GlobalValue *Decl =
                WrapperModules[GI.ModuleName]->getNamedValue(Ref.first);
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
            PluginScopeImportVariables[GI.ModuleName][Ref.first] = MergedVar;
          }
        }
      }
    }
    if (!Changed)
      break;
  }

  // Find GIs that are directly referenced from the merged module or the wrapper
  // module.
  for (auto &Item : GlobalItems) {
    GlobalItem &GI = Item.second;
    GI.AvailableExternallyInWrapperModule = true;
    bool RefFromMerged = GI.DefineInMergedModule ||
                         (!GI.PartID.empty() && !GI.BodyInWrapperModule);
    for (auto &Ref : GI.Refs) {
      auto Res = Resolve(GI.ModuleName, Ref.first);
      if (RefFromMerged && GI.RefersToPluginScope)
        if (PluginScopeImportVariables[GI.ModuleName].count(Ref.first))
          continue;
      if (Res.GI && RefFromMerged)
        Res.GI->NeededInMergedModule = true;
      if (Res.GI && !RefFromMerged)
        Res.GI->NeededInWrapperModule = true;
    }
  }

  while (true) {
    bool Changed = false;
    for (auto &Item : GlobalItems) {
      GlobalValue *GV = Item.first;
      GlobalItem &GI = Item.second;
      if (!GI.AvailableExternallyInWrapperModule)
        continue;
      if (GV->hasLocalLinkage() && GI.DefineInMergedModule &&
          !GI.NeededInWrapperModule) {
        GI.AvailableExternallyInWrapperModule = false;
        Changed = true;
      }
      SmallPtrSet<GlobalValue *, 8> Refs, ForcedSameModule;
      Refs = FindGlobalReferences(GV, &ForcedSameModule);
      if (!ForcedSameModule.empty()) {
        GI.AvailableExternallyInWrapperModule = false;
        Changed = true;
        continue;
      }
      for (GlobalValue *TargetGV : Refs) {
        if (GlobalItems.count(TargetGV)) {
          GlobalItem &Target = GlobalItems[TargetGV];
          if (!Target.AvailableExternallyInWrapperModule) {
            GI.AvailableExternallyInWrapperModule = false;
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
        !GI.BodyInWrapperModule) {
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
        GI.NeededInWrapperModule) {
      // The definition will go in the merged module. But the wrapper module may
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
      errs() << "  define in "
             << (GI.DefineInMergedModule ? "merged" : "wrapper") << "\n";
      errs() << "  body in " << (GI.BodyInWrapperModule ? "wrapper" : "merged")
             << "\n";
      if (GV->hasLocalLinkage())
        errs() << "  local\n";
      if (GI.NeededInWrapperModule)
        errs() << "  needed in wrapper\n";
      if (GI.NeededInMergedModule)
        errs() << "  needed in merged\n";
      if (GI.AvailableExternallyInMergedModule)
        errs() << "  available externally in merged module\n";
      if (GI.AvailableExternallyInWrapperModule)
        errs() << "  available externally in wrapper module\n";
      errs() << "  export count: " << ExportedCount[GI.Name] << "\n";
      errs() << "  new name: " << GI.NewName << "\n";
    }
  }
}

ResolvedReference GLMerger::Resolve(StringRef ModuleName, StringRef Name) {
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

void GLMerger::MakeAvailableExternally(GlobalValue *GV) {
  LinkageMap.erase(GV);
  if (GlobalObject *GO = dyn_cast<GlobalObject>(GV)) {
    GV->setLinkage(GlobalValue::AvailableExternallyLinkage);
    GO->setComdat(nullptr);
    GV->setVisibility(GlobalValue::DefaultVisibility);
    GV->setDSOLocal(false);
  } else if (isa<GlobalAlias>(GV)) {
    GV->setLinkage(GlobalValue::InternalLinkage);
    GV->setVisibility(GlobalValue::DefaultVisibility);
    GV->setDSOLocal(true);
  }
}

// Check whether the constant can be replaced with a dynamically loaded value
// or not. If a global object can't be replaced, we can't support RTLD_LOCAL
// lookup of it.
static bool mustStayConstant(Constant *C) {
  for (Value::use_iterator UI = C->use_begin(); UI != C->use_end(); ++UI) {
    if (isa<Function>(UI->getUser()))
      return true; // Used as a function's personality.
    if (isa<LandingPadInst>(UI->getUser()))
      return true; // Used as typeinfo in catch.
    if (Constant *User = dyn_cast<Constant>(UI->getUser()))
      if (mustStayConstant(User))
        return true;
  }
  return false;
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

void GLMerger::FixupPartDefinition(GlobalItem &GI, Function &Body) {
  if (GI.BodyInWrapperModule || !GI.RefersToPluginScope)
    return;
  StringMap<GlobalVariable *> &ImportVars =
      PluginScopeImportVariables[GI.ModuleName];
  for (GlobalObject &GO : Body.getParent()->global_objects()) {
    if (ImportVars.count(GO.getName()) && !mustStayConstant(&GO)) {
      expandConstant(&GO, &Body);
      GlobalVariable *Var = new GlobalVariable(
          *Body.getParent(), GO.getType(), false, GlobalValue::ExternalLinkage,
          nullptr, ImportVars[GO.getName()]->getName());
      IRBuilder<> Builder(&Body.getEntryBlock().front());
      Value *Load = Builder.CreateLoad(Var);
      GO.replaceAllUsesWith(Load);
    }
  }
}

GlobalValue *GLMerger::LoadPartDefinition(GlobalItem &GI, Module *M) {
  if (!GI.DefineInMergedModule && GI.BodyInWrapperModule) {
    GlobalValue *Result =
        Merger::LoadPartDefinition(GI, WrapperModules[GI.ModuleName].get());
    return Result;
  } else {
    return Merger::LoadPartDefinition(GI, M);
  }
}

void GLMerger::AddPartStub(Module &MergedModule, GlobalItem &GI,
                           GlobalValue *Def, GlobalValue *Decl,
                           StringRef NewName) {
  if (NewName.empty())
    NewName = GI.NewName;
  Module &WrapperModule = *WrapperModules[GI.ModuleName];

  if (GI.DefineInMergedModule) {
    Merger::AddPartStub(MergedModule, GI, Def, Decl, NewName);

    if (GI.NeededInWrapperModule) {
      // Export the symbol from the merged module.
      GlobalValue *NewStub = MergedModule.getNamedValue(NewName);
      LinkageMap[NewStub] = GlobalValue::ExternalLinkage;
      NewStub->setLinkage(GlobalValue::ExternalLinkage);
      NewStub->setVisibility(GlobalValue::ProtectedVisibility);

      // Import the symbol into the wrapper module.
      GlobalValue *StubInWrapperModule = WrapperModule.getNamedValue(GI.Name);
      ReplaceGlobal(WrapperModule, NewName, StubInWrapperModule);
      LinkageMap[StubInWrapperModule] = GlobalValue::ExternalLinkage;
      StubInWrapperModule->setLinkage(GlobalValue::ExternalLinkage);
      convertToDeclaration(*StubInWrapperModule);
      StubInWrapperModule->setDSOLocal(false);
    }
  } else {
    if (!GI.BodyInWrapperModule) {
      // Export the body from the merged module.
      LinkageMap[Def] = GlobalValue::ExternalLinkage;
      Def->setLinkage(GlobalValue::ExternalLinkage);
      Def->setVisibility(GlobalValue::ProtectedVisibility);
    }

    // Import the body into the wrapper module.
    Function *BodyDecl = WrapperModule.getFunction(Def->getName());
    if (!BodyDecl) {
      BodyDecl = Function::Create(cast<Function>(Def)->getFunctionType(),
                                  GlobalValue::ExternalLinkage, Def->getName(),
                                  &WrapperModule);
    }
    assert(BodyDecl->getName() == Def->getName());
    assert(BodyDecl->getFunctionType() ==
           cast<Function>(Def)->getFunctionType());
    Merger::AddPartStub(WrapperModule, GI, BodyDecl, Decl, GI.Name);
    GlobalValue *WrapperStub = WrapperModule.getNamedValue(GI.Name);

    if (GlobalValue::isLocalLinkage(LinkageMap[WrapperStub]) &&
        GI.NeededInMergedModule) {
      LinkageMap.erase(WrapperStub);
      ReplaceGlobal(WrapperModule, GI.NewName, WrapperStub);
      WrapperStub->setLinkage(GlobalValue::ExternalLinkage);
      WrapperStub->setVisibility(GlobalValue::ProtectedVisibility);
    } else if (GI.Name != GI.NewName) {
      // If we have an alternate NewName, we need an alias.
      ReplaceGlobal(WrapperModule, GI.NewName,
                    GlobalAlias::create(GlobalValue::ExternalLinkage,
                                        GI.NewName, WrapperStub));
    }

    if (GI.AvailableExternallyInMergedModule) {
      // Add an available_externally definition to the merged module.
      Merger::AddPartStub(MergedModule, GI, Def, Decl, GI.NewName);
      MakeAvailableExternally(MergedModule.getNamedValue(NewName));
    }
  }
}

void GLMerger::LoadRemainder(std::unique_ptr<Module> M,
                             std::vector<GlobalItem *> &GIs) {
  Module &WrapperModule = *WrapperModules[M->getModuleIdentifier()];
  std::vector<GlobalItem *> GIsToMerge;

  // Make everything internal by default, unless we actually need it.
  for (GlobalValue &GV :
       concat<GlobalValue>(WrapperModule.global_objects(),
                           WrapperModule.aliases(), WrapperModule.ifuncs())) {
    if (!GV.isDeclarationForLinker() && !LinkageMap.count(&GV) &&
        !GV.getName().startswith("__bcdb_"))
      GV.setLinkage(GlobalValue::InternalLinkage);
  }

  for (GlobalItem *GI : GIs) {
    if (GI->DefineInMergedModule) {

      // Define in the merged module.
      GIsToMerge.push_back(GI);
      if (GI->NeededInWrapperModule) {
        // Define private globals in the merged module, but export them so the
        // wrapper module can use them.
        GlobalValue *GV = M->getNamedValue(GI->NewName);
        GV->setLinkage(GlobalValue::ExternalLinkage);
        GV->setVisibility(GlobalValue::DefaultVisibility);
        GV->setDSOLocal(false);
      }

      // Make the wrapper module's version available_externally.
      GlobalValue *NewGV = WrapperModule.getNamedValue(GI->Name);
      ReplaceGlobal(WrapperModule, GI->NewName, NewGV);
      if (!NewGV->isDeclaration()) {
        if (GI->AvailableExternallyInWrapperModule) {
          assert(!M->getNamedValue(GI->NewName)->hasLocalLinkage());
          MakeAvailableExternally(NewGV);
        } else {
          if (!convertToDeclaration(*NewGV))
            NewGV = nullptr;
        }
      }

    } else {
      // Export the definition from the wrapper module.
      GlobalValue *GV = M->getNamedValue(GI->NewName);
      GlobalValue *NewGV = WrapperModule.getNamedValue(GI->Name);
      LinkageMap.erase(NewGV);
      NewGV->setLinkage(GV->getLinkage());
      NewGV->setDSOLocal(GV->isDSOLocal());

      if (NewGV->hasLocalLinkage() && GI->NeededInMergedModule) {
        ReplaceGlobal(WrapperModule, GI->NewName, NewGV);
        NewGV->setLinkage(GlobalValue::ExternalLinkage);
        NewGV->setVisibility(GlobalValue::ProtectedVisibility);
      } else if (GI->Name != GI->NewName) {
        // If we have an alternate NewName, we need an alias.
        ReplaceGlobal(WrapperModule, GI->NewName,
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

// Check whether the call instruction has a type attribute (such as byval or
// sret) with the wrong type.
static bool hasBadTypeAttributes(const CallBase &CB) {
  AttributeList attributes = CB.getAttributes();
  for (unsigned i = 0; i < CB.arg_size(); ++i)
    for (Attribute attr : attributes.getAttributes(i + 1))
      if (attr.isTypeAttribute())
        if (attr.getValueAsType() !=
            CB.getArgOperand(i)->getType()->getPointerElementType())
          return true;
  return false;
}

// Fix type attributes (such as byval or sret) to use the correct type.
static void fixBadTypeAttributes(CallBase &CB) {
  if (!hasBadTypeAttributes(CB))
    return;
  AttributeList attrs = CB.getAttributes();
  AttributeList orig_attrs = attrs;
  for (unsigned i = 0; i < CB.arg_size(); ++i) {
    for (Attribute attr : orig_attrs.getAttributes(i + 1)) {
      if (attr.isTypeAttribute()) {
        attrs =
            attrs.removeAttribute(CB.getContext(), i + 1, attr.getKindAsEnum());
        attrs = attrs.addAttribute(
            CB.getContext(), i + 1,
            Attribute::get(
                CB.getContext(), attr.getKindAsEnum(),
                CB.getArgOperand(i)->getType()->getPointerElementType()));
      }
    }
  }
  CB.setAttributes(attrs);
  assert(!hasBadTypeAttributes(CB));
}

std::unique_ptr<Module> GLMerger::Finish() {
  auto M = Merger::Finish();

  // Ensure that all the bad type attributes we fix (see below) are introduced
  // by InstCombine, and none have been introduced by the merging process
  // itself.
  for (const auto &F : *M)
    for (const auto &BB : F)
      for (const auto &I : BB)
        if (const CallBase *CB = dyn_cast<CallBase>(&I))
          assert(!hasBadTypeAttributes(*CB));

  if (!DisableOpts) {
    // Run some optimizations to make use of the available_externally functions
    // we created.
    legacy::PassManager PM;
    PM.add(createInstructionCombiningPass(/*ExpensiveCombines*/ true));
#if LLVM_VERSION_MAJOR >= 12
    PM.add(createInstSimplifyLegacyPass());
#else
    PM.add(createConstantPropagationPass());
#endif
    PM.add(createAlwaysInlinerLegacyPass());
    PM.add(createGlobalDCEPass());
    PM.run(*M);
  }

  // As of LLVM 12.0.0, InstCombine has a bug where it can break type
  // attributes (byval, sret, etc.):
  // https://bugs.llvm.org/show_bug.cgi?id=50697
  //
  // As a workaround, we fix the attribute type ourselves.
  for (auto &F : *M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (CallBase *CB = dyn_cast<CallBase>(&I))
          fixBadTypeAttributes(*CB);

  Linker::linkModules(*M, LoadGLLibrary(M->getContext()));
  FunctionType *UndefFuncType =
      M->getFunction("__bcdb_unreachable_function_called")->getFunctionType();
  Function *WeakDefCalled;
  if (WeakModule)
    WeakDefCalled =
        Function::Create(UndefFuncType, GlobalValue::ExternalLinkage,
                         "__bcdb_weak_definition_called", WeakModule.get());

  for (auto &Item : WrapperModules) {
    StringRef ModuleName = Item.first();
    Module &WrapperModule = *Item.second;

    StringMap<GlobalVariable *> &ImportVars =
        PluginScopeImportVariables[Item.first()];
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
            WrapperModule.getNamedValue(Name), Types.back()));
      }
      StructType *SType =
          StructType::create(Types, ("__bcdb_imports_" + ModuleName).str());
      PointerType *PType = SType->getPointerTo();

      Function *Callee = Function::Create(
          FunctionType::get(Type::getVoidTy(WrapperModule.getContext()),
                            {PType}, false),
          GlobalValue::ExternalLinkage, "__bcdb_set_imports_" + ModuleName,
          M.get());

      {
        Function *Decl = Function::Create(Callee->getFunctionType(),
                                          GlobalValue::ExternalLinkage,
                                          Callee->getName(), &WrapperModule);
        Constant *Value = ConstantStruct::get(SType, Values);
        GlobalVariable *StubVar = new GlobalVariable(
            WrapperModule, SType, true, GlobalValue::ExternalLinkage, Value,
            ("__bcdb_imports_" + ModuleName).str());
        Function *F = Function::Create(
            FunctionType::get(Type::getVoidTy(WrapperModule.getContext()),
                              false),
            GlobalValue::InternalLinkage, "__bcdb_init_imports",
            &WrapperModule);
        BasicBlock *BB = BasicBlock::Create(F->getContext(), "", F);
        IRBuilder<> Builder(BB);
        Builder.CreateCall(Decl, {StubVar});
        Builder.CreateRetVoid();
        appendToGlobalCtors(WrapperModule, F, 0);
      }

      BasicBlock *BB = BasicBlock::Create(Callee->getContext(), "", Callee);
      IRBuilder<> Builder(BB);
      for (size_t i = 0; i < Vars.size(); i++) {
        GlobalVariable *Var = Vars[i];
        Value *Ptr = Builder.CreateStructGEP(SType, Callee->arg_begin(), i);
        Value *Val = Builder.CreateLoad(Ptr);
        Builder.CreateStore(Val, Var);
        Var->setLinkage(GlobalValue::InternalLinkage);
      }
      Builder.CreateRetVoid();
    }

    // Prevent deletion of linkonce globals--they may be needed by the merged
    // module.
    for (GlobalValue &GV :
         concat<GlobalValue>(WrapperModule.global_objects(),
                             WrapperModule.aliases(), WrapperModule.ifuncs())) {
      if (GV.hasLinkOnceLinkage()) {
        GlobalValue *Used = M->getNamedValue(GV.getName());
        if (Used && !Used->use_empty() && !Used->hasExactDefinition())
          GV.setLinkage(
              GlobalValue::getWeakLinkage(GV.hasLinkOnceODRLinkage()));
      }
    }

    // Remove anything we didn't decide to export.
    std::unique_ptr<ModulePass> DCEPass(createGlobalDCEPass());
    DCEPass->runOnModule(WrapperModule);
  }

  // Make weak definitions for everything declared in the merged module. That
  // way we can link against the merged library even if we're not linking
  // against any particular wrapper library.
  for (GlobalObject &GO : M->global_objects()) {
    if (!GO.isDeclarationForLinker())
      continue;

    if (symbolInSection("gl-always-defined-externally", "", GO.getName()))
      continue;

    if (GlobalVariable *Var = dyn_cast<GlobalVariable>(&GO)) {
      convertToDeclaration(*Var);
      Var->setLinkage(GlobalValue::ExternalWeakLinkage);
      Var->setVisibility(GlobalValue::DefaultVisibility);
      Var->setDSOLocal(false);
    } else if (Function *F = dyn_cast<Function>(&GO)) {
      convertToDeclaration(*F);
      F->setLinkage(GlobalValue::ExternalWeakLinkage);
      F->setVisibility(GlobalValue::DefaultVisibility);
      F->setDSOLocal(false);
      if (WeakModule) {
        F = Function::Create(F->getFunctionType(), GlobalValue::WeakAnyLinkage,
                             F->getAddressSpace(), F->getName(),
                             WeakModule.get());
        BasicBlock *BB = BasicBlock::Create(F->getContext(), "", F);
        IRBuilder<> Builder(BB);
        Builder.CreateCall(WeakDefCalled,
                           {Builder.CreateGlobalStringPtr(GO.getName())});
        Builder.CreateUnreachable();
      }
    }
  }

  StringSet<> MustExport;
  for (auto &Item : WrapperModules) {
    Module &WrapperModule = *Item.second;
    for (GlobalObject &GO : WrapperModule.global_objects())
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
      if (!mayHaveExternalOverrides(GI.ModuleName, GI.NewName)) {
        if (!GO->isDefinitionExact())
          GO->setLinkage(GlobalValue::ExternalLinkage);
        if (!GO->hasLocalLinkage() && GO->hasDefaultVisibility() &&
            isa<Function>(GO))
          GO->setVisibility(GlobalValue::ProtectedVisibility);
      }

      // If we know there are no users outside the merged module, internalize
      // it.
      if (!mayHaveDynamicUses(GI.ModuleName, GI.NewName) &&
          !MustExport.count(GI.NewName)) {
        GO->setLinkage(GlobalValue::InternalLinkage);
      }
    }
  }

  if (TrapUnreachableFunctions) {
    DiagnoseUnreachableFunctions(*M, UndefFuncType);
    for (auto &Item : WrapperModules) {
      Module &WrapperModule = *Item.second;
      DiagnoseUnreachableFunctions(WrapperModule, UndefFuncType);
    }
  }

  if (DisableDSOLocal) {
    for (GlobalValue &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      GV.setVisibility(GlobalValue::DefaultVisibility);
      if (!GV.hasLocalLinkage())
        GV.setDSOLocal(false);
    }
    for (auto &Item : WrapperModules) {
      Module &WrapperModule = *Item.second;
      for (GlobalValue &GV : concat<GlobalValue>(WrapperModule.global_objects(),
                                                 WrapperModule.aliases(),
                                                 WrapperModule.ifuncs())) {
        GV.setVisibility(GlobalValue::DefaultVisibility);
        if (!GV.hasLocalLinkage())
          GV.setDSOLocal(false);
      }
    }
  }

  return M;
}

std::unique_ptr<llvm::Module> BCDB::GuidedLinker(
    std::vector<llvm::StringRef> Names,
    llvm::StringMap<std::unique_ptr<llvm::Module>> &WrapperModules,
    std::unique_ptr<llvm::Module> *WeakModule) {
  GLMerger Merger(*this, WeakModule != nullptr);
  for (StringRef Name : Names) {
    Merger.AddModule(Name);
  }
  Merger.PrepareToRename();
  Merger.RenameEverything();
  auto Result = Merger.Finish();

  if (WeakModule)
    *WeakModule = std::move(Merger.WeakModule);
  WrapperModules = std::move(Merger.WrapperModules);
  return Result;
}
