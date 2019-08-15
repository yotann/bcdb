#include "bcdb/BCDB.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
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
template <> struct llvm::GraphTraits<GlobalGraph *> {
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
GlobalSet llvm::GraphTraits<GlobalGraph *>::Empty;

// - for each SCC:
//   - make a structure of the element Names, element IDs/initializers, and the
//   referent_name->group pairs (or just groups ordered by referent_name)
//   - look up the structure in the group table
//     - if it exists, redirect element Names -> group -> new Names
//     - if it doesn't exist, add a new one and assign new Names
// - import everything using the new Names
//   - if group.Name already has a GV, just reuse that
//   - if group.Name doesn't have a GV yet, we need to add one

namespace {
class Entry {
public:
  std::string Name;
  std::string Def;
  bool HasID;
  mutable std::string NewName;
  mutable std::string NewID;

  bool operator<(const Entry &Other) const {
    if (Name < Other.Name)
      return true;
    if (Name > Other.Name)
      return false;
    if (Def < Other.Def)
      return true;
    if (Def > Other.Def)
      return false;
    return HasID < Other.HasID;
  }
};

class Group {
public:
  std::vector<Entry> Entries;
  std::vector<const Group *> Refs;

  void AddEntry(std::string Name, std::string Def, bool HasID) {
    Entries.push_back({std::move(Name), std::move(Def), HasID, {}, {}});
  }

  void AddRef(const Group *Ref) { Refs.push_back(Ref); }

  void Finish() {
    std::sort(Entries.begin(), Entries.end());
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

  StringRef GetNewName(StringRef Name) const {
    for (const Entry &E : Entries) {
      if (E.Name == Name)
        return E.NewName;
    }
    return ""; // FIXME
  }

  StringRef GetNewID(StringRef Name) const {
    for (const Entry &E : Entries) {
      if (E.Name == Name)
        return E.NewID;
    }
    return ""; // FIXME
  }
};
} // end anonymous namespace

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
  StringMap<GlobalValue::LinkageTypes> LinkageMap;
  std::set<Group> Groups;
  std::set<std::string> AssignedNames;

  Expected<GlobalValue *> LoadID(StringRef ID, StringRef Name,
                                 std::map<std::string, std::string> &NewNames);
  Expected<GlobalSet> LoadRefs(StringRef ID, Module &Remainder);
  void MakeWrapper(GlobalValue *GV, StringRef Name);
  void ReplaceGlobal(StringRef Name, GlobalValue *New);
  void AssignNewNames(const Group &Group);
};
} // end anonymous namespace

void Merger::AssignNewNames(const Group &Group) {
  for (const Entry &E : Group.Entries) {
    int i = 0;
    E.NewName = E.Name;
    while (AssignedNames.count(E.NewName)) {
      E.NewName = E.Name + "." + to_string(i++);
    }
    AssignedNames.insert(E.NewName);

    if (E.HasID) {
      i = 0;
      E.NewID = "__bcdb_id_" + E.Def;
      while (AssignedNames.count(E.NewID)) {
        E.NewID = "__bcdb_id_" + E.Def + "." + to_string(i++);
      }
      AssignedNames.insert(E.NewID);
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

void Merger::MakeWrapper(GlobalValue *GV, StringRef Name) {
  // errs() << "making wrapper " << Name << " for function " << GV->getName()
  //        << " in module " << GV->getParent() << "\n";
  Function *F = cast<Function>(GV);
  if (F->isVarArg()) {
    // No easy way to do this: https://stackoverflow.com/q/7015477
    ValueToValueMapTy VMap;
    Function *G = CloneFunction(F, VMap, /* CodeInfo */ nullptr);
    ReplaceGlobal(Name, G);
    return;
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
  // errs() << "Loading " << ID << "\n";
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
  if (Error Err = Mover.move(std::move(MPart), {Def},
                             [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
                             /* LinkModuleInlineAsm */ false,
#endif
                             /* IsPerformingImport */ false))
    return std::move(Err);

  LinkageMap[Name] = GlobalValue::InternalLinkage;
  return M->getNamedValue(Name);
}

Error Merger::Add(StringRef Name) {
  errs() << "Adding " << Name << "\n";

  std::map<std::string, std::string> PartIDs;
  auto RemainderOrErr = bcdb.LoadParts(Name, PartIDs);
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

  std::map<GlobalValue *, const Group *> LocalGroups;
  std::map<std::string, std::string> NewNames;
  for (auto &SCC : make_range(scc_begin(&Graph), scc_end(&Graph))) {
    Group G;
    for (auto &X : SCC) {
      GlobalValue *GV = X.second;
      if (GV) {
        std::string Def;
        bool HasID;
        if (PartIDs.count(GV->getName())) {
          Def = PartIDs[GV->getName()];
          HasID = true;
        } else if (!GV->isDeclaration()) {
          Def = to_string(*GV);
          HasID = false;
        }
        G.AddEntry(GV->getName(), std::move(Def), HasID);
        for (GlobalValue *Ref : Graph.G[GV]) {
          G.AddRef(LocalGroups[Ref]);
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
          LocalGroups[GV] = &*I.first;
          if (!GV->isDeclaration())
            NewNames[GV->getName()] = I.first->GetNewName(GV->getName());
          else
            NewNames[GV->getName()] = GV->getName();
        }
      }
    }
  }

  // Make all globals external so function modules can link to them.
  for (GlobalObject &GO : Remainder->global_objects()) {
    LinkageMap[NewNames[GO.getName()]] = GO.getLinkage();
    GO.setLinkage(GlobalValue::ExternalLinkage);
  }

  // FIXME: we're missing opportunities to deduplicate functions with different
  // names

  // need to figure out if this ID has already been added in a reusable way
  // if not, store the ID somewhere
  for (const auto &item : PartIDs) {
    StringRef Name = item.first;
    StringRef ID = item.second;

    const Group *G = LocalGroups[Remainder->getNamedValue(Name)];
    auto NewName = G->GetNewName(Name);
    auto NewID = G->GetNewID(Name);

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

    MakeWrapper(GV, NewName);
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
        GV2 = M->getOrInsertFunction(GV->getName(),
                                     cast<FunctionType>(GV->getValueType()));
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
      return std::move(Err);
  }
  auto M = Merger.TakeModule();
  // errs() << *M;
  return std::move(M);
}
