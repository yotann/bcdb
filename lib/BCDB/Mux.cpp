#include "bcdb/BCDB.h"

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include <map>
#include <set>

#include "Merge.h"

namespace {
#include "data/mux_main.inc"
}

using namespace bcdb;
using namespace llvm;

static std::unique_ptr<Module> LoadMainModule(LLVMContext &Context) {
  ExitOnError Err("LoadMainModule: ");
  StringRef Buffer(reinterpret_cast<char *>(mux_main_bc), mux_main_bc_len);
  auto MainMod =
      Err(parseBitcodeFile(MemoryBufferRef(Buffer, "main"), Context));
  MainMod->setTargetTriple("");
  return MainMod;
}

class MuxMerger : public Merger {
public:
  MuxMerger(BCDB &bcdb) : Merger(bcdb) {}
  ResolvedReference Resolve(StringRef ModuleName, StringRef Name) override;
  void PrepareToRename();
  std::unique_ptr<Module> Finish();

private:
  struct MainEntry {
    std::string name;
    GlobalItem *main;
    GlobalItem *global_ctors;
    GlobalItem *global_dtors;
    GlobalItem *used;
    GlobalItem *compiler_used;
  };

  void HandleUsed(Module &M, bool compiler, GlobalItem *GI);
  Constant *HandleInitFini(Module &M, GlobalItem *GI);
  Constant *HandleEntry(Module &M, const MainEntry &Entry);

  StructType *EntryType;
  PointerType *InitType;
  GlobalVariable *InitEmpty;

  StringMap<SmallVector<StringRef, 1>> GlobalDefs;
  StringMap<SmallVector<StringRef, 1>> GlobalWeakDefs;
};

ResolvedReference MuxMerger::Resolve(StringRef ModuleName, StringRef Name) {
  GlobalValue *GV = ModRemainders[ModuleName]->getNamedValue(Name);
  if (GV && !GV->isDeclaration())
    return ResolvedReference(&GlobalItems[GV]);
  auto &Defs = GlobalDefs[Name];
  if (Defs.empty()) {
    auto &WeakDefs = GlobalWeakDefs[Name];
    if (WeakDefs.empty())
      return ResolvedReference(Name); // dynamic linking
    else
      return ResolvedReference(
          &GlobalItems[ModRemainders[WeakDefs[0]]->getNamedValue(
              Name)]); // choose an arbitrary weak definition
  } else if (Defs.size() == 1) {
    return ResolvedReference(&GlobalItems[ModRemainders[Defs[0]]->getNamedValue(
        Name)]); // only one definition, link to it
  } else {
    errs() << "multiple definitions of " << Name << ":\n";
    for (auto &Def : Defs)
      errs() << "- defined in " << Def << "\n";
    errs() << "- used in " << ModuleName << "\n";
    report_fatal_error("multiple definitions of " + Name);
  }
}

void MuxMerger::PrepareToRename() {
  ReserveName("main");
  ReserveName("__bcdb_main");
  ReserveName("llvm.global_ctors");
  ReserveName("llvm.global_dtors");
  ReserveName("llvm.used");
  ReserveName("llvm.compiler.used");

  for (auto &Item : GlobalItems) {
    GlobalValue *GV = Item.first;
    GlobalItem &GI = Item.second;
    if (GV->hasExternalLinkage())
      GlobalDefs[GI.Name].push_back(GI.ModuleName);
    if (GV->hasWeakLinkage())
      GlobalWeakDefs[GI.Name].push_back(GI.ModuleName);
    // TODO: other linkage types
  }
}

void MuxMerger::HandleUsed(Module &M, bool compiler, GlobalItem *GI) {
  if (!GI)
    return;
  GlobalVariable *GV = cast<GlobalVariable>(M.getNamedValue(GI->NewName));
  if (!GV->hasInitializer())
    return;
  const ConstantArray *Init = cast<ConstantArray>(GV->getInitializer());
  SmallVector<GlobalValue *, 8> Globals;
  for (Value *Op : Init->operands())
    Globals.push_back(cast<GlobalValue>(Op->stripPointerCasts()));
  (compiler ? appendToCompilerUsed : appendToUsed)(M, Globals);
  // TODO: is it possible for the GV to be used by something else?
  GV->eraseFromParent();
}

Constant *MuxMerger::HandleInitFini(Module &M, GlobalItem *GI) {
  if (!GI)
    return InitEmpty;
  GlobalVariable *GV = cast<GlobalVariable>(M.getNamedValue(GI->NewName));
  if (GV->hasAppendingLinkage())
    GV->setLinkage(GlobalValue::PrivateLinkage);
  assert(GV->hasUniqueInitializer());
  if (isa<ConstantAggregateZero>(GV->getInitializer()))
    return InitEmpty;
  std::vector<Constant *> Fns;
  for (auto &V : cast<ConstantArray>(GV->getInitializer())->operands()) {
    if (isa<ConstantAggregateZero>(V))
      continue;
    ConstantStruct *CS = cast<ConstantStruct>(V);
    if (isa<ConstantPointerNull>(CS->getOperand(1)))
      continue;
    ConstantInt *CI = cast<ConstantInt>(CS->getOperand(0));
    assert(CI->getZExtValue() == 65535);
    Constant *F = CS->getOperand(1)->stripPointerCasts();
    assert(isa<Function>(F));
    if (F->getType() != InitType)
      F = ConstantExpr::getPointerCast(F, InitType);
    Fns.push_back(F);
  }
  Fns.push_back(ConstantPointerNull::get(InitType));
  auto CA = ConstantArray::get(ArrayType::get(InitType, Fns.size()), Fns);
  auto *G = new GlobalVariable(M, CA->getType(), /* isConstant */ true,
                               GlobalValue::PrivateLinkage, CA);

  Constant *Zero = ConstantInt::get(Type::getInt32Ty(M.getContext()), 0);
  Constant *Indices[] = {Zero, Zero};
  return ConstantExpr::getInBoundsGetElementPtr(G->getValueType(), G, Indices);
}

Constant *MuxMerger::HandleEntry(Module &M, const MainEntry &Entry) {
  IRBuilder<> Builder(&M.getFunction("main")->front());
  Constant *Name = cast<Constant>(Builder.CreateGlobalStringPtr(Entry.name));
  Constant *Main = cast<Constant>(M.getNamedValue(Entry.main->NewName));
  if (Main->getType() != EntryType->getElementType(1))
    Main = ConstantExpr::getPointerCast(Main, EntryType->getElementType(1));
  Constant *Init = HandleInitFini(M, Entry.global_ctors);
  Constant *Fini = HandleInitFini(M, Entry.global_dtors);
  SmallVector<Constant *, 4> Elements{Name, Main, Init, Fini};
  return ConstantStruct::get(EntryType, Elements);
}

std::unique_ptr<Module> MuxMerger::Finish() {
  std::vector<MainEntry> MainEntries;
  for (auto &Item : ModRemainders) {
    MainEntry Entry;
    Entry.name = sys::path::filename(Item.first());
    GlobalValue *GV = Item.second->getNamedValue("main");
    Entry.main = GV ? &GlobalItems[GV] : nullptr;
    GV = Item.second->getNamedValue("llvm.global_ctors");
    Entry.global_ctors = GV ? &GlobalItems[GV] : nullptr;
    GV = Item.second->getNamedValue("llvm.global_dtors");
    Entry.global_dtors = GV ? &GlobalItems[GV] : nullptr;
    GV = Item.second->getNamedValue("llvm.used");
    Entry.used = GV ? &GlobalItems[GV] : nullptr;
    GV = Item.second->getNamedValue("llvm.compiler.used");
    Entry.compiler_used = GV ? &GlobalItems[GV] : nullptr;
    MainEntries.push_back(Entry);
  }

  auto M = Merger::Finish();

  for (auto &GV : M->global_objects())
    if (!GV.isDeclaration())
      GV.setLinkage(GlobalValue::InternalLinkage);

  // Prevent LLVM from deleting functions that will be used by the code
  // generator.
  // TODO: apply this to *all* libcalls. See llvm::updateCompilerUsed().
  GlobalValue *UnwindResume = M->getNamedValue("_Unwind_Resume");
  if (UnwindResume)
    appendToCompilerUsed(*M, {UnwindResume});

  Linker::linkModules(*M, LoadMainModule(M->getContext()));

  GlobalVariable *StubMain = M->getGlobalVariable("__bcdb_main");
  EntryType = cast<StructType>(StubMain->getValueType());
  InitType =
      cast<PointerType>(EntryType->getElementType(2)->getPointerElementType());
  InitEmpty = new GlobalVariable(*M, InitType, /* isConstant */ true,
                                 GlobalValue::PrivateLinkage,
                                 ConstantPointerNull::get(InitType));

  std::vector<Constant *> ConstantEntries;
  for (auto &Entry : MainEntries) {
    HandleUsed(*M, false, Entry.used);
    HandleUsed(*M, true, Entry.compiler_used);
    if (!Entry.main) {
      // FIXME actually call these
      HandleInitFini(*M, Entry.global_ctors);
      HandleInitFini(*M, Entry.global_dtors);
      continue;
    }
    ConstantEntries.push_back(HandleEntry(*M, Entry));
  }
  ConstantEntries.push_back(ConstantAggregateZero::get(EntryType));

  auto Array = ConstantArray::get(
      ArrayType::get(EntryType, ConstantEntries.size()), ConstantEntries);
  auto *GV = new GlobalVariable(*M, Array->getType(), /* isConstant */ true,
                                GlobalValue::PrivateLinkage, Array);
  Constant *Zero = ConstantInt::get(Type::getInt32Ty(M->getContext()), 0);
  Constant *Indices[] = {Zero, Zero};
  Constant *GEP =
      ConstantExpr::getInBoundsGetElementPtr(GV->getValueType(), GV, Indices);
  StubMain->replaceAllUsesWith(GEP);
  StubMain->eraseFromParent();
  GV->setName("__bcdb_main");

  return M;
}

Expected<std::unique_ptr<Module>> BCDB::Mux(std::vector<StringRef> Names) {
  MuxMerger Merger(*this);
  for (StringRef Name : Names)
    Merger.AddModule(Name);
  Merger.PrepareToRename();
  Merger.RenameEverything();
  return Merger.Finish();
}
