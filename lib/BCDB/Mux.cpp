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

#include <map>
#include <set>

namespace {
#include "data/mux_main.inc"
}

using namespace bcdb;
using namespace llvm;

Expected<std::unique_ptr<Module>> BCDB::Mux(std::vector<StringRef> Names) {
  std::map<std::pair<std::string, std::string>, Value *> Mapping;
  auto MOrErr = Merge(Names, Mapping);
  if (!MOrErr)
    return MOrErr.takeError();
  auto M = std::move(*MOrErr);

  for (auto &GV : M->global_objects())
    if (!GV.isDeclaration())
      GV.setLinkage(GlobalValue::InternalLinkage);

  for (auto &Name :
       {"main", "__bcdb_main", "llvm.global_ctors", "llvm.global_dtors"})
    if (auto GV = M->getNamedValue(Name))
      GV->setName("");

  StringRef Buffer(reinterpret_cast<char *>(mux_main_bc), mux_main_bc_len);
  auto MainModOrErr =
      parseBitcodeFile(MemoryBufferRef(Buffer, "main"), *Context);
  if (!MainModOrErr)
    return MainModOrErr.takeError();
  auto MainMod = std::move(*MainModOrErr);
  MainMod->setTargetTriple("");

  Linker::linkModules(*M, std::move(MainMod));

  IRBuilder<> Builder(&M->getFunction("main")->front());
  GlobalVariable *StubMain = M->getGlobalVariable("__bcdb_main");
  StructType *EntryType = cast<StructType>(StubMain->getValueType());
  PointerType *InitType =
      cast<PointerType>(EntryType->getElementType(2)->getPointerElementType());
  auto *InitEmpty = new GlobalVariable(*M, InitType, /* isConstant */ true,
                                       GlobalValue::PrivateLinkage,
                                       ConstantPointerNull::get(InitType));

  auto handleInitFini = [&](Value *V) -> Constant * {
    if (!V)
      return InitEmpty;
    GlobalVariable *GV = cast<GlobalVariable>(V);
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
    auto CA = ConstantArray::get(ArrayType::get(InitType, Fns.size()), Fns);
    auto *G = new GlobalVariable(*M, CA->getType(), /* isConstant */ true,
                                 GlobalValue::PrivateLinkage, CA);

    Constant *Zero = ConstantInt::get(Type::getInt32Ty(*Context), 0);
    Constant *Indices[] = {Zero, Zero};
    return ConstantExpr::getInBoundsGetElementPtr(G->getValueType(), G,
                                                  Indices);
  };

  std::vector<Constant *> Entries;
  for (auto Name : Names) {
    if (!Mapping[std::make_pair(Name, "main")]) {
      // FIXME actually call these
      handleInitFini(Mapping[std::make_pair(Name, "llvm.global_ctors")]);
      handleInitFini(Mapping[std::make_pair(Name, "llvm.global_dtors")]);
      continue;
    }
    auto Base = sys::path::filename(Name);
    Constant *EntryName = cast<Constant>(Builder.CreateGlobalStringPtr(Base));
    Constant *EntryMain = cast<Constant>(Mapping[std::make_pair(Name, "main")]);
    if (EntryMain->getType() != EntryType->getElementType(1))
      EntryMain =
          ConstantExpr::getPointerCast(EntryMain, EntryType->getElementType(1));
    Constant *EntryInit =
        handleInitFini(Mapping[std::make_pair(Name, "llvm.global_ctors")]);
    Constant *EntryFini =
        handleInitFini(Mapping[std::make_pair(Name, "llvm.global_dtors")]);
    Entries.push_back(ConstantStruct::get(EntryType, EntryName, EntryMain,
                                          EntryInit, EntryFini));
  }
  Entries.push_back(ConstantAggregateZero::get(EntryType));

  auto Array =
      ConstantArray::get(ArrayType::get(EntryType, Entries.size()), Entries);
  auto *GV = new GlobalVariable(*M, Array->getType(), /* isConstant */ true,
                                GlobalValue::PrivateLinkage, Array);

  Constant *Zero = ConstantInt::get(Type::getInt32Ty(*Context), 0);
  Constant *Indices[] = {Zero, Zero};
  Constant *GEP =
      ConstantExpr::getInBoundsGetElementPtr(GV->getValueType(), GV, Indices);
  StubMain->replaceAllUsesWith(GEP);
  StubMain->eraseFromParent();

  return std::move(M);
}
