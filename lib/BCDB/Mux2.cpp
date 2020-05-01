#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <map>

#include "Merge.h"
#include "bcdb/LLVMCompat.h"

namespace {
#include "mux2_library.inc"
}

using namespace bcdb;
using namespace llvm;

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
// - See also ld's -Bsymbolic option.

class Mux2Merger : public Merger {
public:
  Mux2Merger(BCDB &bcdb);
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
};

Mux2Merger::Mux2Merger(BCDB &bcdb) : Merger(bcdb) {
  EnableMustTail = true;
  EnableNameReuse = false;

  WeakModule = std::make_unique<Module>("weak", bcdb.GetContext());

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
      GlobalValue *NewGV = StubModule.getNamedValue(GI->Name);
      NewGV->setLinkage(GV->getLinkage());

#if LLVM_VERSION_MAJOR >= 7
      NewGV->setDSOLocal(GV->isDSOLocal());
#endif
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
  auto WeakDefCalled = WeakModule->getOrInsertFunction(
      "__bcdb_weak_definition_called", UndefFuncType);

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
    if (!GO.isDeclaration())
      continue;

    static const StringSet<> NO_PLACEHOLDER = {
        // These are weakly defined in libc_nonshared.a; don't override them!
        "atexit",          "at_quick_exit",
        "__fstat",         "fstat",
        "fstat64",         "fstatat",
        "fstatat64",       "__libc_csu_fini",
        "__libc_csu_init", "__lstat",
        "lstat",           "lstat64",
        "__mknod",         "mknod",
        "mknodat",         "__pthread_atfork",
        "pthread_atfork",  "__stack_chk_fail_local",
        "__stat",          "stat",
        "stat64",
    };

    if (NO_PLACEHOLDER.count(GO.getName()))
      continue;

    if (GlobalVariable *Var = dyn_cast<GlobalVariable>(&GO)) {
      new GlobalVariable(*WeakModule, Var->getValueType(), Var->isConstant(),
                         GlobalValue::WeakAnyLinkage,
                         Constant::getNullValue(Var->getValueType()),
                         Var->getName(), nullptr, Var->getThreadLocalMode(),
                         Var->getAddressSpace());
    } else if (Function *F = dyn_cast<Function>(&GO)) {
      if (F->isIntrinsic())
        continue;
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
           std::unique_ptr<llvm::Module> &WeakModule) {
  Mux2Merger Merger(*this);
  for (StringRef Name : Names) {
    Merger.AddModule(Name);
  }
  Merger.PrepareToRename();
  Merger.RenameEverything();
  auto Result = Merger.Finish();

  WeakModule = std::move(Merger.WeakModule);
  Stubs = std::move(Merger.StubModules);
  return Result;
}
