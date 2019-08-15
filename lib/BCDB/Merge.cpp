#include "bcdb/BCDB.h"

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Transforms/Utils/Cloning.h>

using namespace bcdb;
using namespace llvm;

namespace {
class Merger {
public:
  Merger(BCDB &bcdb)
      : bcdb(bcdb), M(std::make_unique<Module>("muxed", bcdb.GetContext())),
        Mover(*M) {}
  Error Add(StringRef Name);
  std::unique_ptr<Module> TakeModule() {
    for (GlobalObject &GO : M->global_objects())
      if (LinkageMap.count(GO.getName()))
        GO.setLinkage(LinkageMap[GO.getName()]);
    return std::move(M);
  }

private:
  BCDB &bcdb;
  std::unique_ptr<Module> M;
  IRMover Mover;
  std::map<std::string, GlobalValue *> LoadedIDs;
  std::map<std::string, std::string> LoadedNames;
  StringMap<GlobalValue::LinkageTypes> LinkageMap;

  Expected<GlobalValue *> LoadID(StringRef ID);
  void MakeWrapper(GlobalValue *GV, StringRef Name);
};
} // end anonymous namespace

void Merger::MakeWrapper(GlobalValue *GV, StringRef Name) {
  errs() << "making wrapper " << Name << " for function " << GV->getName()
         << " in module " << GV->getParent() << "\n";
  Function *F = cast<Function>(GV);
  if (F->isVarArg()) {
    // No easy way to do this: https://stackoverflow.com/q/7015477
    ValueToValueMapTy VMap;
    Function *G = CloneFunction(F, VMap, /* CodeInfo */ nullptr);
    G->setName(Name);
    return;
  }
  // see llvm::MergeFunctions::writeThunk
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

  auto Name = ("__bcdb_id_" + ID).str();
  if (M->getNamedValue(Name))
    return make_error<StringError>("name conflict for " + Name,
                                   errc::invalid_argument);
  Def->setName(Name);

  // Move the definition into the main module.
  if (Error Err = Mover.move(std::move(MPart), {Def},
                             [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
                             /* LinkModuleInlineAsm */ false,
#endif
                             /* IsPerformingImport */ false))
    return std::move(Err);

  Result = M->getNamedValue(Name);
  LinkageMap[Name] = GlobalValue::InternalLinkage;
  return Result;
}

Error Merger::Add(StringRef Name) {
  errs() << "Adding " << Name << "\n";

  std::map<std::string, std::string> PartIDs;
  auto RemainderOrErr = bcdb.LoadParts(Name, PartIDs);
  if (!RemainderOrErr)
    return RemainderOrErr.takeError();
  std::unique_ptr<Module> Remainder = std::move(*RemainderOrErr);

  // Make all globals external so function modules can link to them.
  for (GlobalObject &GO : Remainder->global_objects()) {
    LinkageMap[GO.getName()] = GO.getLinkage();
    GO.setLinkage(GlobalValue::ExternalLinkage);
  }

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

  std::vector<GlobalValue *> ValuesToLink;
  for (auto &G : concat<GlobalValue>(Remainder->global_objects(), Remainder->aliases(), Remainder->ifuncs())) {
    errs() << "considering global " << G.getName() << "\n";
    if (!PartIDs.count(G.getName())) {
      errs() << "linking global " << G << "\n";
      if (isa<GlobalObject>(G))
        ValuesToLink.push_back(&G);
      GlobalValue *G2 = M->getNamedValue(G.getName());
      if (G2 && !G2->isDeclaration() && !G.isDeclaration()) {
        auto S = to_string(G), S2 = to_string(*G2);
        if (S != S2) {
          return make_error<StringError>("conflicting globals " + S + " and " +
                                             S2,
                                         errc::invalid_argument);
        }
      }
    }
  }

  for (auto &A : Remainder->aliases()) {
    errs() << "rewriting alias " << A << "\n";
    if (auto GV = dyn_cast<GlobalValue>(A.getAliasee()->stripPointerCasts())) {
      Constant *GV2;
      if (GV->getValueType()->isFunctionTy()) {
        GV2 = M->getOrInsertFunction(GV->getName(),
                                     cast<FunctionType>(GV->getValueType()));
      } else {
        GV2 = M->getOrInsertGlobal(GV->getName(), GV->getValueType());
      }
      if (GV2->getType() != A.getType())
        GV2 = ConstantExpr::getBitCast(GV2, A.getType());
      auto A2 = GlobalAlias::create(A.getValueType(), 0, A.getLinkage(),
                                    A.getName(), GV2, M.get());
      GlobalValue *Old = M->getNamedValue(A.getName());
      if (Old != A2) {
        Old->replaceAllUsesWith(A2);
        Old->eraseFromParent();
        A2->setName(A.getName());
      }
      errs() << "got " << *A2 << "\n";
    } else {
      return make_error<StringError>("unsupported alias " + A.getName(),
                                     errc::invalid_argument);
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

  // errs() << *M;

  auto Main = M->getNamedValue("main");
  if (Main) {
    Main->setName("__bcdb_main_" + Name);
    LoadedNames.erase("main");
  }

  return Error::success();
}

Expected<std::unique_ptr<Module>>
BCDB::Merge(std::vector<StringRef> Names,
            std::map<std::pair<std::string, std::string>, Value *> &Mapping) {
  Merger Merger(*this);
  for (StringRef Name : Names) {
    Error Err = Merger.Add(Name);
    if (Err)
      return std::move(Err);
  }
  auto M = Merger.TakeModule();
  // errs() << *M;
  return std::move(M);
}
