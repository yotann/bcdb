#include "Merge.h"

#include <algorithm>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/DOTGraphTraits.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/GraphWriter.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Transforms/IPO.h>
#include <map>
#include <set>
#include <vector>

#include "Util.h"
#include "bcdb/BCDB.h"
#include "bcdb/Split.h"

using namespace bcdb;
using namespace llvm;

cl::OptionCategory bcdb::MergeCategory("Merging options");

static cl::opt<bool> DisableDeduplication("disable-deduplication",
                                          cl::cat(MergeCategory));
static cl::opt<bool> DisableStubs("disable-stubs", cl::cat(MergeCategory));
static cl::opt<bool> WriteGlobalGraph("write-global-graph",
                                      cl::cat(MergeCategory));

Merger::Merger(BCDB &bcdb)
    : bcdb(bcdb),
      MergedModule(std::make_unique<Module>("merged", bcdb.GetContext())) {}

void Merger::AddModule(StringRef ModuleName) {
  // Load remainder module.
  ExitOnError Err("Merger::AddModule: ");
  auto &Remainder = ModRemainders[ModuleName];
  std::map<std::string, std::string> PartIDs;
  Remainder = Err(bcdb.LoadParts(ModuleName, PartIDs));

  // Definitions marked available_externally are a little tricky to handle, and
  // anyway we'll match the dynamic linker's behavior better if we replace them
  // with declarations.
  std::unique_ptr<ModulePass> elim_avail_extern(
      createEliminateAvailableExternallyPass());
  elim_avail_extern->runOnModule(*Remainder);

  // Find all references to globals.
  for (const auto &item : PartIDs) {
    GlobalValue &GV = *Remainder->getNamedValue(item.first);
    // May have been replaced with a declaration by elim_avail_extern.
    if (!GV.isDeclaration()) {
      GlobalItems[&GV].PartID = item.second;
      for (const auto &ref : LoadPartRefs(item.second, item.first))
        GlobalItems[&GV].Refs[std::string(ref.first())] = ResolvedReference();
    }
  }
  for (GlobalValue &GV :
       concat<GlobalValue>(Remainder->global_objects(), Remainder->aliases(),
                           Remainder->ifuncs())) {
    if (GV.isDeclaration())
      continue;
    if (!GlobalItems.count(&GV))
      for (const auto &Ref : FindGlobalReferences(&GV))
        GlobalItems[&GV].Refs[std::string(Ref->getName())] =
            ResolvedReference();
    GlobalItems[&GV].ModuleName = ModuleName;
    GlobalItems[&GV].Name = GV.getName();
  }
}

// Given the ID of a single function definition, find all global names
// referenced by that definition.
StringSet<> Merger::LoadPartRefs(StringRef ID, StringRef SelfName) {
  // TODO: Cache results.
  // TODO: Retain the loaded module to be used by LoadPartDefinition().
  ExitOnError Err("Merger::LoadPartRefs: ");
  auto MPart = Err(bcdb.GetFunctionById(ID));
  StringSet<> Result;
  Function *Def = &getSoleDefinition(*MPart);

  // If the function takes its own address, add a reference using its own name.
  if (!Def->use_empty()) {
    Result.insert(SelfName);
  }

  for (GlobalValue &GV : concat<GlobalValue>(MPart->global_objects(),
                                             MPart->aliases(), MPart->ifuncs()))
    if (GV.hasName())
      Result.insert(GV.getName());
  return Result;
}

StringRef Merger::GetNewName(const ResolvedReference &Ref) {
  if (!Ref.Name.empty())
    return Ref.Name;
  return Ref.GI->NewName;
}

void Merger::ApplyNewNames(
    Module &M, const std::map<std::string, ResolvedReference> &Refs) {
  DenseMap<GlobalValue *, std::string> NewNames;
  StringMap<const ResolvedReference *> NewReferences;
  for (GlobalValue &GV :
       concat<GlobalValue>(M.global_objects(), M.aliases(), M.ifuncs())) {
    if (GV.hasName() && Refs.count(std::string(GV.getName()))) {
      auto &Ref = Refs.at(std::string(GV.getName()));
      auto NewName = GetNewName(Ref);
      NewNames[&GV] = NewName;
      if (NewReferences.count(NewName)) {
        if (*NewReferences[NewName] != Ref) {
          errs() << "module " + M.getModuleIdentifier() << ":\n";
          errs() << "conflicting references for symbol " + NewName << ":\n";
          errs() << "- " << *NewReferences[NewName] << "\n";
          errs() << "- " << Ref << "\n";
          report_fatal_error("conflicting references");
        }
      }
      NewReferences[NewName] = &Ref;
    }
    GV.setName("");
  }
  for (const auto &Item : NewNames) {
    GlobalValue &GV = *Item.first;
    StringRef NewName = Item.second;
    GV.setName(NewName);
    if (GV.getName() != NewName) {
      Constant *GV2 = M.getNamedValue(NewName);
      if (GV2->getType() != GV.getType())
        GV2 = ConstantExpr::getPointerCast(GV2, GV.getType());
      GV.replaceAllUsesWith(GV2);
    }
  }
}

GlobalValue *Merger::LoadPartDefinition(GlobalItem &GI, Module *M) {
  if (!M)
    M = MergedModule.get();
  ExitOnError Err("Merger::LoadPartDefinition: ");
  GlobalValue *Result = M->getNamedValue(GI.NewDefName);
  if (Result && !Result->isDeclaration())
    return Result;
  auto MPart = Err(bcdb.GetFunctionById(GI.PartID));
  Function *Def = &getSoleDefinition(*MPart);

  ApplyNewNames(*MPart, GI.Refs);
  Def->setName(GI.NewDefName);
  assert(Def->getName() == GI.NewDefName);
  if (!DisableStubs && !Def->use_empty()) {
    // If the function takes its own address, redirect it to the stub.
    Function *Decl =
        Function::Create(Def->getFunctionType(), GlobalValue::ExternalLinkage,
                         GI.NewName, MPart.get());
    Decl->copyAttributesFrom(Def);
    Def->replaceAllUsesWith(Decl);
  }
  FixupPartDefinition(GI, *Def);

  // Move the definition into the main module.
  if (M == MergedModule.get())
    Err(MergedModuleMover->move(
        std::move(MPart), {Def},
        [](GlobalValue &GV, IRMover::ValueAdder Add) {},
        /* IsPerformingImport */ false));
  else
    Err(IRMover(*M).move(
        std::move(MPart), {Def},
        [](GlobalValue &GV, IRMover::ValueAdder Add) {},
        /* IsPerformingImport */ false));

  Result = M->getNamedValue(GI.NewDefName);
  LinkageMap[Result] = GlobalValue::InternalLinkage;
  return Result;
}

void Merger::ReplaceGlobal(Module &M, StringRef Name, GlobalValue *New) {
  New->setName(Name);
  GlobalValue *Old = M.getNamedValue(Name);
  if (Old != New) {
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

void Merger::AddPartStub(Module &MergedModule, GlobalItem &GI,
                         GlobalValue *DefGV, GlobalValue *DeclGV,
                         StringRef NewName) {
  Function *Def = cast<Function>(DefGV);
  Function *Decl = cast<Function>(DeclGV);
  GlobalValue *StubGV;
  if (NewName.empty())
    NewName = GI.NewName;

  CallInst::TailCallKind TCK =
      Def->isVarArg() ? CallInst::TCK_MustTail : CallInst::TCK_Tail;

  if (DeclGV->hasGlobalUnnamedAddr() && !DefGV->isDeclaration()) {
    // If the address of the stub doesn't matter, we can just make an alias to
    // the body.
    StubGV = GlobalAlias::create(Def->getLinkage(), NewName, Def);

  } else if (TCK == CallInst::TCK_MustTail && !EnableMustTail &&
             !DefGV->isDeclaration()) {
    // In theory, it should be fine to create stubs for these using musttail.
    // But LLVM's optimizations are buggy and will break the musttail call. As
    // a stopgap we just create an alias, even though this is incorrect in some
    // cases.

    // FIXME: Create an actual stub. Rewrite the definition to take a va_list*
    // instead of ..., then put @llvm.va_start in the stub.
    StubGV = GlobalAlias::create(Def->getLinkage(), NewName, Def);

  } else {

    // see llvm::MergeFunctions::writeThunk
    Function *Stub = Function::Create(Def->getFunctionType(), Def->getLinkage(),
                                      NewName, &MergedModule);
    for (auto I : zip(Stub->args(), Def->args()))
      std::get<0>(I).setName(std::get<1>(I).getName());
    Stub->copyAttributesFrom(Def);
    Stub->removeFnAttr(Attribute::NoInline);
    Stub->removeFnAttr(Attribute::OptimizeNone);
    Stub->addFnAttr(Attribute::AlwaysInline);

    BasicBlock *BB = BasicBlock::Create(Stub->getContext(), "", Stub);
    IRBuilder<> Builder(BB);
    std::vector<Value *> Args;
    for (auto A : zip(Stub->args(), Def->args()))
      Args.push_back(
          Builder.CreatePointerCast(&std::get<0>(A), std::get<1>(A).getType()));
    CallInst *CI = Builder.CreateCall(Def, Args);
    CI->setTailCallKind(TCK);
    CI->setCallingConv(Def->getCallingConv());
    CI->setAttributes(Def->getAttributes());
    if (Stub->getReturnType()->isVoidTy())
      Builder.CreateRetVoid();
    else
      Builder.CreateRet(CI);

    if (Decl->getComdat()) {
      Comdat *CD = MergedModule.getOrInsertComdat(Decl->getComdat()->getName());
      CD->setSelectionKind(Decl->getComdat()->getSelectionKind());
      Stub->setComdat(CD);
    }
    StubGV = Stub;
  }

  ReplaceGlobal(MergedModule, NewName, StubGV);
  LinkageMap[StubGV] = Decl->getLinkage();
  StubGV->setDSOLocal(Decl->isDSOLocal());
}

void Merger::LoadRemainder(std::unique_ptr<Module> M,
                           std::vector<GlobalItem *> &GIs) {
  ExitOnError Err("Merger::LoadRemainder: ");

  StringMap<GlobalValue::LinkageTypes> NameLinkageMap;
  std::vector<GlobalValue *> ValuesToLink;
  for (GlobalItem *GI : GIs) {
    if (GI->SkipStub)
      continue;
    GlobalValue *GV = M->getNamedValue(GI->NewName);

    if (GlobalAlias *A = dyn_cast<GlobalAlias>(GV)) {
      // The alias is currently pointing to a stub in the remainder module. We
      // can't get IRMover to change what the alias refers to, so we have to
      // recreate the alias ourselves. And we can't create the alias during
      // LoadRemainder because the aliasee might be defined in a different
      // module that hasn't been loaded yet.
      // TODO: ifuncs should be handled the same way.
      assert(!AliasMap.count(GV->getName()));
      AliasMap[GI->NewName] = std::make_pair(
          A->getAliasee()->stripPointerCasts()->getName(), A->getLinkage());
    } else {
      NameLinkageMap[GI->NewName] = GV->getLinkage();
      ValuesToLink.push_back(GV);
    }
  }

  // Prevent local symbols from being renamed.
  for (GlobalValue &GV : M->global_objects())
    GV.setLinkage(GlobalValue::ExternalLinkage);

  Err(MergedModuleMover->move(
      std::move(M), ValuesToLink,
      [](GlobalValue &GV, IRMover::ValueAdder Add) {},
      /* IsPerformingImport */ false));

  for (auto &Item : NameLinkageMap)
    LinkageMap[MergedModule->getNamedValue(Item.first())] = Item.second;
}

namespace bcdb {
class MergerGlobalGraph {
public:
  MergerGlobalGraph(Merger *Merger) {
    Root.RefItems.push_back(&Root); // ensure Root.RefItems contains all nodes
    for (auto &item : Merger->GlobalItems) {
      Root.RefItems.push_back(&item.second);
      for (auto &Ref : item.second.Refs) {
        auto Res = Merger->Resolve(item.second.ModuleName, Ref.first);
        Ref.second = Res;
        if (!Res.GI) {
          // reserve the name for dynamic linking
          Merger->ReservedNames.insert(Res.Name);
        } else {
          item.second.RefItems.push_back(Res.GI);
        }
      }
    }
  }
  Merger::GlobalItem Root;
};
} // end namespace bcdb

namespace llvm {
template <> struct GraphTraits<MergerGlobalGraph *> {
  using NodeRef = Merger::GlobalItem *;
  using ChildIteratorType = llvm::SmallVectorImpl<NodeRef>::iterator;
  using nodes_iterator = llvm::SmallVectorImpl<NodeRef>::iterator;
  static NodeRef getEntryNode(MergerGlobalGraph *G) { return &G->Root; }
  static nodes_iterator nodes_begin(MergerGlobalGraph *G) {
    return G->Root.RefItems.begin();
  }
  static nodes_iterator nodes_end(MergerGlobalGraph *G) {
    return G->Root.RefItems.end();
  }
  static inline ChildIteratorType child_begin(NodeRef N) {
    return N->RefItems.begin();
  }
  static inline ChildIteratorType child_end(NodeRef N) {
    return N->RefItems.end();
  }
};
template <>
struct DOTGraphTraits<MergerGlobalGraph *> : public DefaultDOTGraphTraits {
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}
  static std::string getGraphName(MergerGlobalGraph *G) {
    return "Global reference graph";
  }
  // isNodeHidden is called with 1 or 2 arguments depending on the version of
  // LLVM.
  static bool isNodeHidden(Merger::GlobalItem *Node,
                           MergerGlobalGraph *G = nullptr) {
    return Node->Name.empty(); // hide the root node
  }
  std::string getNodeLabel(Merger::GlobalItem *Node, MergerGlobalGraph *G) {
    return Node->ModuleName + ":" + Node->Name;
  }
  static std::string getNodeIdentifierLabel(Merger::GlobalItem *Node,
                                            MergerGlobalGraph *G) {
    return Node->NewName;
  }
  static std::string getNodeDescription(Merger::GlobalItem *Node,
                                        MergerGlobalGraph *G) {
    return Node->NewDefName;
  }
};
} // end namespace llvm

void Merger::RenameEverything() {
  MergerGlobalGraph Graph(this);
  using Group = SmallVector<GlobalItem *, 1>;
  auto ItemComp = [&](GlobalItem *a, GlobalItem *b) {
    if (a->PartID != b->PartID)
      return a->PartID < b->PartID;
    if (a->Refs < b->Refs)
      return true;
    if (b->Refs < a->Refs)
      return false;
    if (a->PartID.empty()) {
      if (false) {
        // attempt to merge global variables
        // FIXME: actually check the initializer
        if (a->Name != b->Name)
          return a->Name < b->Name;
        GlobalValue *GVa = ModRemainders[a->ModuleName]->getNamedValue(a->Name);
        GlobalValue *GVb = ModRemainders[b->ModuleName]->getNamedValue(b->Name);
        if (isa<Function>(GVa) || isa<Function>(GVb))
          return true;
        return GVa->getType() < GVb->getType();
      } else {
        // global variables are never merged
        return true;
      }
    }
    if ((a->RefersToPluginScope && !a->PartID.empty()) ||
        (b->RefersToPluginScope && !b->PartID.empty())) {
      // can't merge, they need to be rewritten to use different variables
      return true;
    }
    return false;
  };
  auto GroupComp = [&](const Group &a, const Group &b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        ItemComp);
  };
  std::set<Group, decltype(GroupComp)> Groups(GroupComp);
  std::set<std::pair<std::string, std::string>> ModuleReservedNames;
  for (auto &const_SCC : make_range(scc_begin(&Graph), scc_end(&Graph))) {
    if (const_SCC.size() == 1 && const_SCC[0] == &Graph.Root)
      continue;
    Group SCC(const_SCC.begin(), const_SCC.end());
    bool CanMerge = true;
    if (CanMerge && !DisableDeduplication) {
      std::sort(SCC.begin(), SCC.end(), ItemComp);
      auto Inserted = Groups.insert(SCC);
      if (!Inserted.second) {
        for (const auto &Tuple : zip_first(SCC, *Inserted.first)) {
          GlobalItem *New = std::get<0>(Tuple);
          GlobalItem *Existing = std::get<1>(Tuple);
          New->NewDefName = Existing->NewDefName;
          // We can reuse NewName from a different module, but not from the
          // same module.
          // TODO: We can't do this in some cases (if the program compares
          // executable.&foo with library.&foo).
          // TODO: Disabled with GL because it can break (e.g., multiple
          // input modules define internal function st_mutex_init and external
          // function st_rwlock_init with same ID).
          if (EnableNameReuse && New->NewName.empty() &&
              !ModuleReservedNames.count(
                  std::make_pair(New->ModuleName, Existing->NewName))) {
            New->NewName = Existing->NewName;
            New->SkipStub = true;
          }
        }
      }
    }
    for (GlobalItem *Item : SCC) {
      if (DisableStubs) {
        if (!Item->PartID.empty()) {
          if (Item->NewDefName.empty())
            Item->NewDefName = ReserveName(Item->Name);
          Item->NewName = Item->NewDefName;
          Item->SkipStub = true;
        }
      } else {
        if (!Item->PartID.empty() && Item->NewDefName.empty())
          Item->NewDefName = ReserveName("__bcdb_body_" + Item->Name);
      }
      if (Item->NewName.empty())
        Item->NewName = ReserveName(Item->Name);
      ModuleReservedNames.insert(
          std::make_pair(Item->ModuleName, Item->NewName));
    }
  }
  if (WriteGlobalGraph)
    WriteGraph(&Graph, "merger_global_graph");
}

std::unique_ptr<Module> Merger::Finish() {
  // Create the IRMover here so it can get the up-to-date
  // IdentifiedStructTypes.
  MergedModuleMover = std::make_unique<IRMover>(*MergedModule);

  for (auto &MR : ModRemainders) {
    std::unique_ptr<Module> &M = MR.second;
    std::vector<GlobalItem *> GIs;
    std::map<std::string, ResolvedReference> Refs;
    std::vector<std::pair<GlobalValue *, GlobalValue *>> StubsNeeded;
    for (auto &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      if (!GV.isDeclaration()) {
        GlobalItem &GI = GlobalItems[&GV];
        if (!GI.PartID.empty()) {
          GlobalValue *Def = LoadPartDefinition(GI);
          if (!GI.SkipStub)
            StubsNeeded.emplace_back(&GV, Def);
        } else {
          // FIXME: what if refs to a definition in the remainder are resolved
          // to something else?
          Refs[std::string(GV.getName())] = ResolvedReference(&GI);
          GIs.push_back(&GI);
          for (auto &Item : GI.Refs)
            Refs[Item.first] = Item.second;
        }
      }
    }

    // We have to call AddPartStub() in a separate loop here because it can add
    // new types to MergedModule, which breaks MergedModuleMover.
    for (auto &StubNeeded : StubsNeeded) {
      GlobalValue *GV = StubNeeded.first;
      GlobalValue *Def = StubNeeded.second;
      GlobalItem &GI = GlobalItems[GV];
      AddPartStub(*MergedModule, GI, Def, GV);
    }
    MergedModuleMover = std::make_unique<IRMover>(*MergedModule);

    ApplyNewNames(*M, Refs);
    LoadRemainder(std::move(M), GIs);
  }

  for (auto &I : AliasMap) {
    // The type of the alias may change, which is fine.
    GlobalValue *Def = MergedModule->getNamedValue(I.second.first);
    if (!Def)
      report_fatal_error("alias " + I.first() +
                         " should point to missing symbol " + I.second.first);
    GlobalAlias *NewAlias =
        GlobalAlias::create(Def->getValueType(), 0, I.second.second, I.first(),
                            Def, MergedModule.get());
    ReplaceGlobal(*MergedModule, I.first(), NewAlias);
  }

  for (auto Item : LinkageMap)
    Item.first->setLinkage(Item.second);

  return std::move(MergedModule);
}

std::string Merger::ReserveName(StringRef Prefix) {
  int i = 0;
  std::string Result = std::string(Prefix);
  while (ReservedNames.count(Result)) {
    Result = (Prefix + "." + to_string(i++)).str();
  }
  ReservedNames.insert(Result);
  return Result;
}

ResolvedReference Merger::Resolve(StringRef ModuleName, StringRef Name) {
  GlobalValue *GV = ModRemainders[ModuleName]->getNamedValue(Name);
  if (GV && !GV->isDeclaration())
    return ResolvedReference(&GlobalItems[GV]);
  else
    return ResolvedReference(Name);
}

Expected<std::unique_ptr<Module>>
BCDB::Merge(const std::vector<StringRef> &Names) {
  Merger Merger(*this);
  for (StringRef Name : Names) {
    Merger.AddModule(Name);
  }
  Merger.RenameEverything();
  return Merger.Finish();
}
