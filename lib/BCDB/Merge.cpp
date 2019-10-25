#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <map>
#include <set>

using namespace bcdb;
using namespace llvm;

using GlobalSet = SmallPtrSet<GlobalValue *, 8>;

namespace {
class ReferentFinder {
  GlobalValue *Root;
  GlobalSet Result;
  void Visit(Value *V);

public:
  ReferentFinder(GlobalValue *Root) : Root(Root) {}
  GlobalSet Finish();
};
} // end anonymous namespace

GlobalSet ReferentFinder::Finish() {
  // TODO: visit function/instruction metadata?
  for (auto &Op : Root->operands())
    Visit(Op);
  if (Function *F = dyn_cast<Function>(Root)) {
    for (BasicBlock &BB : *F)
      for (Instruction &I : BB)
        for (const Use &Op : I.operands())
          Visit(Op);
  }
  return std::move(Result);
}

void ReferentFinder::Visit(Value *V) {
  // TODO: check for MetadataAsValue?
  if (V == Root)
    return;
  if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    Result.insert(GV);
  } else if (Constant *C = dyn_cast<Constant>(V)) {
    for (auto &Op : C->operands())
      Visit(Op);
  }
}

namespace {
class GlobalGraph {
public:
  DenseMap<GlobalValue *, GlobalSet> G;
  void Set(GlobalValue *Key, GlobalSet Value) {
    G[nullptr].insert(Key);
    G[Key] = std::move(Value);
  }
};
} // end anonymous namespace

// TODO: this is a horrible mess because we need to keep the GlobalGraph*
// around as part of NodeRef.
namespace llvm {

template <> struct GraphTraits<GlobalGraph *> {
  static GlobalSet Empty;
  using NodeRef = std::pair<GlobalGraph *, GlobalValue *>;
  struct ChildIteratorType {
    GlobalGraph *G;
    GlobalSet::iterator I;
    bool operator==(const ChildIteratorType &Other) const {
      return I == Other.I;
    }
    bool operator!=(const ChildIteratorType &Other) const {
      return I != Other.I;
    }
    ChildIteratorType operator++(int) { return {G, I++}; }
    NodeRef operator*() const { return {G, *I}; }
  };
  static NodeRef getEntryNode(GlobalGraph *G) { return {G, nullptr}; }
  static inline ChildIteratorType child_begin(NodeRef N) {
    if (N.first->G.count(N.second))
      return {N.first, N.first->G[N.second].begin()};
    else
      return {N.first, Empty.begin()};
  }
  static inline ChildIteratorType child_end(NodeRef N) {
    if (N.first->G.count(N.second))
      return {N.first, N.first->G[N.second].end()};
    else
      return {N.first, Empty.end()};
  }
};
GlobalSet GraphTraits<GlobalGraph *>::Empty;

} // end namespace llvm

namespace {
class Group;

class Entry {
public:
  std::string Def;
  bool HasID;
  mutable std::string NewID;
  mutable StringMap<std::string> NewNames;

  bool operator<(const Entry &Other) const {
    if (Def < Other.Def)
      return true;
    if (Def > Other.Def)
      return false;
    return HasID < Other.HasID;
  }

  bool operator==(const Entry &Other) const {
    return Def == Other.Def && HasID == Other.HasID;
  }
};

class Group {
public:
  std::vector<Entry> Entries;
  std::vector<std::pair<std::string, const Entry *>> Refs;

  void AddEntry(std::string Def, bool HasID) {
    Entries.push_back({std::move(Def), HasID, {}, {}});
  }

  void AddRef(StringRef Name, const Entry *Ref) {
    Refs.emplace_back(Name, Ref);
  }

  void Finish() {
    std::sort(Entries.begin(), Entries.end());
    Entries.erase(std::unique(Entries.begin(), Entries.end()), Entries.end());
    std::sort(Refs.begin(), Refs.end());
    Refs.erase(std::unique(Refs.begin(), Refs.end()), Refs.end());
  }

  bool operator<(const Group &Other) const {
    if (Entries < Other.Entries)
      return true;
    if (Entries > Other.Entries)
      return false;
    return Refs < Other.Refs;
  }

  const Entry *GetEntry(StringRef Def) const {
    for (const Entry &E : Entries) {
      if (E.Def == Def)
        return &E;
    }
    return nullptr;
  }
};
} // end anonymous namespace

namespace {
class Merger {
public:
  Merger(BCDB &bcdb,
         std::map<std::pair<std::string, std::string>, Value *> &Mapping)
      : bcdb(bcdb), M(std::make_unique<Module>("muxed", bcdb.GetContext())),
        Mover(*M), Mapping(Mapping) {}
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
  std::map<std::pair<std::string, std::string>, Value *> &Mapping;
  StringMap<GlobalValue::LinkageTypes> LinkageMap;
  std::set<Group> Groups;
  std::set<std::string> AssignedNames;

  Expected<GlobalValue *> LoadID(StringRef ID, StringRef Name,
                                 std::map<std::string, std::string> &NewNames);
  Expected<GlobalSet> LoadRefs(StringRef ID, Module &Remainder);
  Function *MakeWrapper(GlobalValue *GV, StringRef Name);
  void ReplaceGlobal(StringRef Name, GlobalValue *New);
  void AssignNewNames(const Group &Group);
  std::string ReserveName(StringRef Prefix);
};
} // end anonymous namespace

std::string Merger::ReserveName(StringRef Prefix) {
  int i = 0;
  std::string Result = Prefix;
  while (AssignedNames.count(Result)) {
    Result = (Prefix + "." + to_string(i++)).str();
  }
  AssignedNames.insert(Result);
  return Result;
}

void Merger::AssignNewNames(const Group &Group) {
  for (const Entry &E : Group.Entries) {
    if (E.HasID) {
      E.NewID = ReserveName("__bcdb_id_" + E.Def);
    }
  }
}

void Merger::ReplaceGlobal(StringRef Name, GlobalValue *New) {
  New->setName(Name);
  GlobalValue *Old = M->getNamedValue(Name);
  if (Old != New) {
    assert(Old->isDeclaration());
    // We might need a cast if the old declaration had an opaque pointer where
    // the new definition has a struct pointer, or vice versa.
    Old->replaceAllUsesWith(
        Old->getType() == New->getType()
            ? New
            : ConstantExpr::getPointerCast(New, Old->getType()));
    Old->eraseFromParent();
    New->setName(Name);
  }
}

Function *Merger::MakeWrapper(GlobalValue *GV, StringRef Name) {
  Function *F = cast<Function>(GV);
  if (F->isVarArg()) {
    // No easy way to do this: https://stackoverflow.com/q/7015477
    ValueToValueMapTy VMap;
    Function *G = CloneFunction(F, VMap, /* CodeInfo */ nullptr);
    ReplaceGlobal(Name, G);
    return G;
  }

  // see llvm::MergeFunctions::writeThunk
  Function *G =
      Function::Create(F->getFunctionType(), F->getLinkage(), Name, M.get());
  G->copyAttributesFrom(F);

  BasicBlock *BB = BasicBlock::Create(G->getContext(), "", G);
  IRBuilder<> Builder(BB);
  std::vector<Value *> Args;
  for (auto A : zip(G->args(), F->args()))
    Args.push_back(
        Builder.CreatePointerCast(&std::get<0>(A), std::get<1>(A).getType()));
  CallInst *CI = Builder.CreateCall(F, Args);
  CI->setTailCall();
  CI->setCallingConv(F->getCallingConv());
  CI->setAttributes(F->getAttributes());
  if (G->getReturnType()->isVoidTy()) {
    Builder.CreateRetVoid();
  } else {
    Builder.CreateRet(CI);
  }

  ReplaceGlobal(Name, G);
  return G;
}

Expected<GlobalSet> Merger::LoadRefs(StringRef ID, Module &Remainder) {
  // TODO: cache results
  // TODO: retain the loaded module to be used by LoadID()
  auto MPartOrErr = bcdb.GetFunctionById(ID);
  if (!MPartOrErr)
    return MPartOrErr.takeError();
  auto MPart = std::move(*MPartOrErr);
  GlobalSet Result;
  for (auto &G : concat<GlobalValue>(MPart->global_objects(), MPart->aliases(),
                                     MPart->ifuncs())) {
    if (G.hasName())
      Result.insert(Remainder.getNamedValue(G.getName()));
  }
  return std::move(Result);
}

static void ApplyNewNames(Module &M,
                          std::map<std::string, std::string> &NewNames) {
  for (GlobalValue &GV :
       concat<GlobalValue>(M.global_objects(), M.aliases(), M.ifuncs())) {
    if (GV.hasName()) {
      auto NewName = NewNames[GV.getName()];
      GV.setName(NewName);
      assert(GV.getName() == NewName);
    }
  }
}

Expected<GlobalValue *>
Merger::LoadID(StringRef ID, StringRef Name,
               std::map<std::string, std::string> &NewNames) {
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

  ApplyNewNames(*MPart, NewNames);

  assert(!M->getNamedValue(Name));
  Def->setName(Name);

  // Move the definition into the main module.
  if (Error Err = Mover.move(
          std::move(MPart), {Def},
          [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
          /* LinkModuleInlineAsm */ false,
#endif
          /* IsPerformingImport */ false))
    return std::move(Err);

  LinkageMap[Name] = GlobalValue::InternalLinkage;
  return M->getNamedValue(Name);
}

Error Merger::Add(StringRef ModuleName) {
  errs() << "Adding " << ModuleName << "\n";

  std::map<std::string, std::string> PartIDs;
  auto RemainderOrErr = bcdb.LoadParts(ModuleName, PartIDs);
  if (!RemainderOrErr)
    return RemainderOrErr.takeError();
  std::unique_ptr<Module> Remainder = std::move(*RemainderOrErr);

  GlobalGraph Graph;
  for (GlobalValue &GV :
       concat<GlobalValue>(Remainder->global_objects(), Remainder->aliases(),
                           Remainder->ifuncs())) {
    ReferentFinder Ref(&GV);
    Graph.Set(&GV, Ref.Finish());
  }
  for (const auto &item : PartIDs) {
    StringRef Name = item.first;
    StringRef ID = item.second;
    auto RefsOrErr = LoadRefs(ID, *Remainder);
    if (!RefsOrErr)
      return RefsOrErr.takeError();
    Graph.Set(Remainder->getNamedValue(Name), std::move(*RefsOrErr));
  }

  std::map<GlobalValue *, const Entry *> LocalEntries;
  std::map<std::string, std::string> NewNames;
  ModuleSlotTracker MST(Remainder.get());
  for (auto &SCC : make_range(scc_begin(&Graph), scc_end(&Graph))) {
    Group G;
    for (auto &X : SCC) {
      GlobalValue *GV = X.second;
      if (GV) {
        std::string Def;
        bool HasID = false;
        if (PartIDs.count(GV->getName())) {
          Def = PartIDs[GV->getName()];
          HasID = true;
        } else if (!GV->isDeclaration()) {
          raw_string_ostream os(Def);
          GV->print(os, MST);
        }
        G.AddEntry(std::move(Def), HasID);
        for (GlobalValue *Ref : Graph.G[GV]) {
          G.AddRef(Ref->getName(), LocalEntries[Ref]);
        }
      }
    }
    if (!G.Entries.empty()) {
      G.Finish();
      auto I = Groups.insert(std::move(G));
      if (I.second)
        AssignNewNames(*I.first);
      for (auto &X : SCC) {
        GlobalValue *GV = X.second;
        if (GV) {
          std::string Def;
          if (PartIDs.count(GV->getName())) {
            Def = PartIDs[GV->getName()];
          } else if (!GV->isDeclaration()) {
            raw_string_ostream os(Def);
            GV->print(os, MST);
          }
          auto Entry = I.first->GetEntry(Def);
          LocalEntries[GV] = Entry;
          if (!GV->isDeclaration()) {
            auto &NewName = Entry->NewNames[GV->getName()];
            if (NewName.empty()) {
              NewName = ReserveName(GV->getName());
            }
            NewNames[GV->getName()] = NewName;
          } else
            NewNames[GV->getName()] = GV->getName();
        }
      }
    }
  }

  // Make all globals external so function modules can link to them.
  for (GlobalObject &GO : Remainder->global_objects()) {
    auto NewName = NewNames[GO.getName()];
    LinkageMap[NewName] = GO.getLinkage();
    GO.setLinkage(GlobalValue::ExternalLinkage);
  }

  for (const auto &item : PartIDs) {
    StringRef Name = item.first;
    StringRef ID = item.second;

    const Entry *Entry = LocalEntries[Remainder->getNamedValue(Name)];
    auto NewName = NewNames[Name];
    auto NewID = Entry->NewID;

    errs() << Name << ' ' << ID << ' ' << NewName << ' ' << NewID << '\n';

    if (M->getNamedValue(NewName) &&
        !M->getNamedValue(NewName)->isDeclaration())
      continue;

    auto GV = M->getNamedValue(NewID);
    if (!GV || GV->isDeclaration()) {
      auto GVOrErr = LoadID(ID, NewID, NewNames);
      if (!GVOrErr)
        return GVOrErr.takeError();
      GV = *GVOrErr;
    }

    Function *Def = MakeWrapper(GV, NewName);
    Function *Stub = Remainder->getFunction(Name);
    Def->copyAttributesFrom(Stub);
    if (Stub->getComdat()) {
      Comdat *CD = M->getOrInsertComdat(Stub->getComdat()->getName());
      CD->setSelectionKind(Stub->getComdat()->getSelectionKind());
      Def->setComdat(CD);
    }
  }

  ApplyNewNames(*Remainder, NewNames);

  std::vector<GlobalValue *> ValuesToLink;
  for (auto &G :
       concat<GlobalValue>(Remainder->global_objects(), Remainder->aliases(),
                           Remainder->ifuncs())) {
    if (!M->getNamedValue(G.getName()) ||
        M->getNamedValue(G.getName())->isDeclaration())
      if (isa<GlobalObject>(G) && !G.isDeclaration())
        ValuesToLink.push_back(&G);
  }

  for (auto &A : Remainder->aliases()) {
    GlobalValue *Old = M->getNamedValue(A.getName());
    if (Old && !Old->isDeclaration())
      continue;
    if (auto GV = dyn_cast<GlobalValue>(A.getAliasee()->stripPointerCasts())) {
      Constant *GV2;
      if (GV->getValueType()->isFunctionTy()) {
#if LLVM_VERSION_MAJOR >= 9
        GV2 = cast<Constant>(
            M->getOrInsertFunction(GV->getName(),
                                   cast<FunctionType>(GV->getValueType()))
                .getCallee());
#else
        GV2 = M->getOrInsertFunction(GV->getName(),
                                     cast<FunctionType>(GV->getValueType()));
#endif
      } else {
        GV2 = M->getOrInsertGlobal(GV->getName(), GV->getValueType());
      }
      if (GV2->getType() != A.getType())
        GV2 = ConstantExpr::getBitCast(GV2, A.getType());
      auto A2 = GlobalAlias::create(A.getValueType(), 0, A.getLinkage(),
                                    A.getName(), GV2, M.get());
      ReplaceGlobal(A.getName(), A2);
    } else {
      return make_error<StringError>("unsupported alias " + A.getName(),
                                     errc::invalid_argument);
    }
  }

  Error Err = Mover.move(
      std::move(Remainder), ValuesToLink,
      [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
      /* LinkModuleInlineAsm */ false,
#endif
      /* IsPerformingImport */ false);
  if (Err)
    return Err;

  for (auto &N : NewNames) {
    if (LinkageMap[N.second] != GlobalValue::InternalLinkage &&
        LinkageMap[N.second] != GlobalValue::PrivateLinkage) {
      Mapping[std::make_pair(ModuleName, N.first)] = M->getNamedValue(N.second);
    }
  }

  return Error::success();
}

Expected<std::unique_ptr<Module>>
BCDB::Merge(std::vector<StringRef> Names,
            std::map<std::pair<std::string, std::string>, Value *> &Mapping) {
  Merger Merger(*this, Mapping);
  for (StringRef Name : Names) {
    Error Err = Merger.Add(Name);
    if (Err)
      return std::move(Err);
  }
  auto M = Merger.TakeModule();
  return std::move(M);
}
