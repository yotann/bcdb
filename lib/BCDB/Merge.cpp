#include "Merge.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/DOTGraphTraits.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/GraphWriter.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <map>
#include <set>

using namespace bcdb;
using namespace llvm;

static cl::opt<bool> DisableStubs("disable-stubs",
                                  cl::sub(*cl::AllSubCommands));
static cl::opt<bool> WriteGlobalGraph("write-global-graph",
                                      cl::sub(*cl::AllSubCommands));

static StringSet<> FindGlobalReferences(GlobalValue *Root) {
  StringSet<> Result;
  SmallVector<Value *, 8> Todo;

  // TODO: visit function/instruction metadata?
  for (auto &Op : Root->operands())
    Todo.push_back(Op);
  if (Function *F = dyn_cast<Function>(Root))
    for (BasicBlock &BB : *F)
      for (Instruction &I : BB)
        for (const Use &Op : I.operands())
          Todo.push_back(Op);

  while (!Todo.empty()) {
    Value *V = Todo.pop_back_val();
    // TODO: check for MetadataAsValue?
    if (V == Root)
      continue;
    if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
      Result.insert(GV->getName());
    } else if (Constant *C = dyn_cast<Constant>(V)) {
      for (auto &Op : C->operands())
        Todo.push_back(Op);
    }
  }
  return Result;
}

Merger::Merger(BCDB &bcdb)
    : bcdb(bcdb),
      MergedModule(std::make_unique<Module>("merged", bcdb.GetContext())),
      MergedModuleMover(*MergedModule) {}

void Merger::AddModule(StringRef ModuleName) {
  // Load remainder module.
  ExitOnError Err("Merger::AddModule: ");
  auto &Remainder = ModRemainders[ModuleName];
  std::map<std::string, std::string> PartIDs;
  Remainder = Err(bcdb.LoadParts(ModuleName, PartIDs));

  // Find all references to globals.
  for (const auto &item : PartIDs) {
    GlobalValue *GV = Remainder->getNamedValue(item.first);
    GlobalItems[GV].PartID = item.second;
    for (const auto &ref : LoadPartRefs(item.second))
      GlobalItems[GV].Refs[ref.first()] = ResolvedReference();
  }
  for (GlobalValue &GV :
       concat<GlobalValue>(Remainder->global_objects(), Remainder->aliases(),
                           Remainder->ifuncs())) {
    if (GV.isDeclaration())
      continue;
    if (!GlobalItems.count(&GV))
      for (const auto &ref : FindGlobalReferences(&GV))
        GlobalItems[&GV].Refs[ref.first()] = ResolvedReference();
    GlobalItems[&GV].ModuleName = ModuleName;
    GlobalItems[&GV].Name = GV.getName();
  }
}

// Given the ID of a single function definition, find all global names
// referenced by that definition.
StringSet<> Merger::LoadPartRefs(StringRef ID) {
  // TODO: Cache results.
  // TODO: Retain the loaded module to be used by LoadPartDefinition().
  ExitOnError Err("Merger::LoadPartRefs: ");
  auto MPart = Err(bcdb.GetFunctionById(ID));
  StringSet<> Result;
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
  for (GlobalValue &GV :
       concat<GlobalValue>(M.global_objects(), M.aliases(), M.ifuncs())) {
    if (GV.hasName() && Refs.count(GV.getName())) {
      auto NewName = GetNewName(Refs.at(GV.getName()));
      NewNames[&GV] = NewName;
      GV.setName("");
    }
  }
  for (const auto &Item : NewNames) {
    GlobalValue &GV = *Item.first;
    StringRef NewName = Item.second;
    GV.setName(NewName);
    if (GV.getName() != NewName) {
      if (DisableStubs) {
        Constant *GV2 = M.getNamedValue(NewName);
        if (GV2->getType() != GV.getType())
          GV2 = ConstantExpr::getPointerCast(GV2, GV.getType());
        GV.replaceAllUsesWith(GV2);
      } else {
        report_fatal_error("conflicting uses of name " + NewName + " in " +
                           M.getModuleIdentifier() + "\n");
      }
    }
  }
}

GlobalValue *Merger::LoadPartDefinition(GlobalItem &GI) {
  ExitOnError Err("Merger::LoadPartDefinition: ");
  GlobalValue *Result = MergedModule->getNamedValue(GI.NewDefName);
  if (Result && !Result->isDeclaration())
    return Result;
  auto MPart = Err(bcdb.GetFunctionById(GI.PartID));

  Function *Def = nullptr;
  for (Function &F : *MPart) {
    if (!F.isDeclaration()) {
      if (Def) {
        Err(make_error<StringError>("multiple functions in function module " +
                                        GI.PartID,
                                    errc::invalid_argument));
      }
      Def = &F;
    }
  }

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

  // Move the definition into the main module.
  Err(MergedModuleMover.move(
      std::move(MPart), {Def}, [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
      /* LinkModuleInlineAsm */ false,
#endif
      /* IsPerformingImport */ false));

  Result = MergedModule->getNamedValue(GI.NewDefName);
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
                         GlobalValue *DefGV, GlobalValue *DeclGV) {
  Function *Def = cast<Function>(DefGV);
  Function *Decl = cast<Function>(DeclGV);

  if (Def->isVarArg()) {
    // In theory, it should be fine to create stubs for these using musttail.
    // But LLVM's optimizations are buggy and will break the musttail call. As
    // a stopgap we just create an alias, even though this is incorrect in some
    // cases.

    // FIXME: Create an actual stub. Rewrite the definition to take a va_list*
    // instead of ..., then put @llvm.va_start in the stub.

    GlobalAlias *Stub = GlobalAlias::create(Def->getLinkage(), GI.NewName, Def);
    ReplaceGlobal(MergedModule, GI.NewName, Stub);
    LinkageMap[Stub] = Decl->getLinkage();
    return;
  }

  // see llvm::MergeFunctions::writeThunk
  Function *Stub = Function::Create(Def->getFunctionType(), Def->getLinkage(),
                                    GI.NewName, &MergedModule);
  Stub->copyAttributesFrom(Def);
  BasicBlock *BB = BasicBlock::Create(Stub->getContext(), "", Stub);
  IRBuilder<> Builder(BB);
  std::vector<Value *> Args;
  for (auto A : zip(Stub->args(), Def->args()))
    Args.push_back(
        Builder.CreatePointerCast(&std::get<0>(A), std::get<1>(A).getType()));
  CallInst *CI = Builder.CreateCall(Def, Args);
  CI->setTailCall();
  CI->setCallingConv(Def->getCallingConv());
  CI->setAttributes(Def->getAttributes());
  if (Stub->getReturnType()->isVoidTy())
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(CI);

  ReplaceGlobal(MergedModule, GI.NewName, Stub);
  LinkageMap[Stub] = Decl->getLinkage();
  if (Decl->getComdat()) {
    Comdat *CD = MergedModule.getOrInsertComdat(Decl->getComdat()->getName());
    CD->setSelectionKind(Decl->getComdat()->getSelectionKind());
    Stub->setComdat(CD);
  }
}

void Merger::LoadRemainder(std::unique_ptr<Module> M,
                           std::vector<GlobalItem *> &GIs) {
  ExitOnError Err("Merger::LoadRemainder: ");

  StringMap<GlobalValue::LinkageTypes> NameLinkageMap;
  std::vector<GlobalValue *> ValuesToLink;
  std::vector<std::pair<std::string, std::string>> AliasesToLink;
  for (GlobalItem *GI : GIs) {
    if (GI->SkipStub)
      continue;
    GlobalValue *GV = M->getNamedValue(GI->NewName);
    NameLinkageMap[GI->NewName] = GV->getLinkage();

    if (isa<GlobalAlias>(GV) && GV->getValueType()->isFunctionTy()) {
      // The alias is currently pointing to a stub in the remainder module. We
      // can't get IRMover to change what the alias refers to, so we have to
      // recreate the alias ourselves.
      // TODO: ifuncs should be handled the same way.
      AliasesToLink.emplace_back(
          GV->getName(),
          cast<GlobalAlias>(GV)->getAliasee()->stripPointerCasts()->getName());
    } else {
      ValuesToLink.push_back(GV);
    }
  }

  // Prevent local symbols from being renamed.
  for (GlobalValue &GV : M->global_objects())
    GV.setLinkage(GlobalValue::ExternalLinkage);

  Err(MergedModuleMover.move(
      std::move(M), ValuesToLink,
      [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
      /* LinkModuleInlineAsm */ false,
#endif
      /* IsPerformingImport */ false));

  for (const auto &I : AliasesToLink) {
    // The type of the alias may change, which is fine.
    GlobalValue *Def = MergedModule->getNamedValue(I.second);
    GlobalAlias *NewAlias = GlobalAlias::create(
        Def->getValueType(), 0, GlobalValue::ExternalLinkage, I.first, Def,
        MergedModule.get());
    ReplaceGlobal(*MergedModule, I.first, NewAlias);
  }

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
  static bool isNodeHidden(Merger::GlobalItem *Node) {
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
    if (CanMerge) {
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
          if (New->NewName.empty() &&
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
          Item->NewDefName = ReserveName("__bcdb_id_" + Item->PartID);
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
  for (auto &MR : ModRemainders) {
    std::unique_ptr<Module> &M = MR.second;

    std::vector<GlobalItem *> GIs;
    std::map<std::string, ResolvedReference> Refs;
    for (auto &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      if (!GV.isDeclaration()) {
        GlobalItem &GI = GlobalItems[&GV];
        if (!GI.PartID.empty()) {
          GlobalValue *Def = LoadPartDefinition(GI);
          if (!GI.SkipStub)
            AddPartStub(*MergedModule, GI, Def, &GV);
        } else {
          // FIXME: what if refs to a definition in the remainder are resolved
          // to something else?
          Refs[GV.getName()] = ResolvedReference(&GI);
          GIs.push_back(&GI);
          for (auto &Item : GI.Refs)
            Refs[Item.first] = Item.second;
        }
      }
    }

    ApplyNewNames(*M, Refs);
    LoadRemainder(std::move(M), GIs);
  }

  for (auto Item : LinkageMap)
    Item.first->setLinkage(Item.second);

  return std::move(MergedModule);
}

std::string Merger::ReserveName(StringRef Prefix) {
  int i = 0;
  std::string Result = Prefix;
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
