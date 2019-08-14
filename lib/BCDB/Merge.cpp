#include "bcdb/BCDB.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>

using namespace bcdb;
using namespace llvm;

namespace {
class Merger {
public:
  Merger(BCDB &bcdb)
      : bcdb(bcdb), M(std::make_unique<Module>("muxed", bcdb.GetContext())),
        Mover(*M) {}
  Error Add(StringRef Name);
  std::unique_ptr<Module> TakeModule() { return std::move(M); }

private:
  BCDB &bcdb;
  std::unique_ptr<Module> M;
  IRMover Mover;
  std::map<std::string, GlobalValue *> LoadedIDs;
  std::map<std::string, std::string> LoadedNames;

  Expected<GlobalValue *> LoadID(StringRef ID);
  void MakeWrapper(GlobalValue *GV, StringRef Name);
};
} // end anonymous namespace

void Merger::MakeWrapper(GlobalValue *GV, StringRef Name) {
  // see llvm::MergeFunctions::writeThunk
  Function *F = cast<Function>(GV);
  Function *G =
      Function::Create(F->getFunctionType(), F->getLinkage(), Name, M.get());
  BasicBlock *BB = BasicBlock::Create(G->getContext(), "", G);
  IRBuilder<> Builder(BB);
  std::vector<Value *> Args;
  for (auto &A : G->args())
    Args.push_back(&A);
  CallInst *CI = Builder.CreateCall(F, Args);
  CI->setTailCall();
  CI->setCallingConv(F->getCallingConv());
  CI->setAttributes(F->getAttributes());
  if (G->getReturnType()->isVoidTy()) {
    Builder.CreateRetVoid();
  } else {
    Builder.CreateRet(CI);
  }
}

Expected<GlobalValue *> Merger::LoadID(StringRef ID) {
  errs() << "Loading " << ID << "\n";
  auto &Result = LoadedIDs[ID];
  if (Result)
    return Result;

  auto MPartOrErr = bcdb.GetFunctionById(ID);
  if (!MPartOrErr)
    return MPartOrErr.takeError();
  auto MPart = std::move(*MPartOrErr);

  Function *Def = nullptr;
  for (Function &F : *MPart) {
    if (!F.isDeclaration()) {
      if (Def) {
        return make_error<StringError>(
            "multiple functions in function module " + ID,
            errc::invalid_argument);
      }
      Def = &F;
    }
  }

  // Move the definition into the main module.
  if (Error Err = Mover.move(std::move(MPart), {Def},
                             [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
                             /* LinkModuleInlineAsm */ false,
#endif
                             /* IsPerformingImport */ false))
    return std::move(Err);

  Result = Def;
  return Result;
}

Error Merger::Add(StringRef Name) {
  errs() << "Adding " << Name << "\n";

  std::map<std::string, std::string> PartIDs;
  auto RemainderOrErr = bcdb.LoadParts(Name, PartIDs);
  if (!RemainderOrErr)
    return RemainderOrErr.takeError();
  std::unique_ptr<Module> Remainder = std::move(*RemainderOrErr);

  for (const auto &item : PartIDs) {
    StringRef Name = item.first;
    StringRef ID = item.second;

    if (LoadedNames.count(Name)) {
      if (LoadedNames[Name] != ID) {
        return make_error<StringError>("conflicting definitions of " + Name,
                                       errc::invalid_argument);
      }
      continue;
    }
    LoadedNames[Name] = ID;

    auto GVOrErr = LoadID(ID);
    if (!GVOrErr)
      return GVOrErr.takeError();
    MakeWrapper(*GVOrErr, Name);
  }

  // - for each unique ID not already loaded:
  //   - melt into M
  // - for each function:
  //   - add wrapper function (rename main)
  // - melt remainder module into M (rename main)

  // TODO: check whether anything in the remainder will conflict
  std::vector<GlobalValue *> ValuesToLink;
  for (auto &G : Remainder->globals()) {
    if (!PartIDs.count(G.getName())) {
      ValuesToLink.push_back(&G);
    }
  }
  Error Err = Mover.move(std::move(Remainder), ValuesToLink,
                         [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
                         /* LinkModuleInlineAsm */ false,
#endif
                         /* IsPerformingImport */ false);
  if (Err)
    return std::move(Err);

  return Error::success();
}

Expected<std::unique_ptr<Module>>
BCDB::Merge(std::vector<StringRef> Names,
            std::map<std::pair<std::string, std::string>, Value *> &Mapping) {
  Merger Merger(*this);
  for (StringRef Name : Names) {
    Error Err = Merger.Add(Name);
    if (Err)
      return Err;
  }
  return Merger.TakeModule();
}
