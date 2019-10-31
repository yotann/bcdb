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

Merger::Merger(BCDB &bcdb) : bcdb(bcdb) {}

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
  if (Ref.Module.empty())
    return Ref.Name;
  GlobalValue *GV = ModRemainders[Ref.Module]->getNamedValue(Ref.Name);
  return GlobalItems[GV].NewName;
}

void Merger::ApplyNewNames(
    Module &M, const std::map<std::string, ResolvedReference> &Refs) {
  for (GlobalValue &GV :
       concat<GlobalValue>(M.global_objects(), M.aliases(), M.ifuncs())) {
    if (GV.hasName() && Refs.count(GV.getName())) {
      // FIXME: what if something already has the name?
      auto NewName = GetNewName(Refs.at(GV.getName()));
      GV.setName(NewName);
      assert(GV.getName() == NewName);
    }
  }
}

GlobalValue *Merger::LoadPartDefinition(Module &MergedModule, GlobalItem &GI) {
  ExitOnError Err("Merger::LoadPartDefinition: ");
  GlobalValue *Result = MergedModule.getNamedValue(GI.NewDefName);
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
  if (!Def->use_empty()) {
    // If the function takes its own address, redirect it to the stub.
    Function *Decl =
        Function::Create(Def->getFunctionType(), GlobalValue::ExternalLinkage,
                         GI.NewName, MPart.get());
    Decl->copyAttributesFrom(Def);
    Def->replaceAllUsesWith(Decl);
  }

  IRMover Mover(MergedModule); // TODO: don't recreate every time
  // Move the definition into the main module.
  Err(Mover.move(
      std::move(MPart), {Def}, [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
      /* LinkModuleInlineAsm */ false,
#endif
      /* IsPerformingImport */ false));

  Result = MergedModule.getNamedValue(GI.NewDefName);
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
  CI->setTailCallKind(CallInst::TCK_MustTail);
  CI->setCallingConv(Def->getCallingConv());
  CI->setAttributes(Def->getAttributes());
  if (Stub->getReturnType()->isVoidTy())
    Builder.CreateRetVoid();
  else
    Builder.CreateRet(CI);

  ReplaceGlobal(MergedModule, GI.NewName, Stub);
  LinkageMap[Stub] = Decl->getLinkage();
  Stub->copyAttributesFrom(Decl);
  if (Decl->getComdat()) {
    Comdat *CD = MergedModule.getOrInsertComdat(Decl->getComdat()->getName());
    CD->setSelectionKind(Decl->getComdat()->getSelectionKind());
    Stub->setComdat(CD);
  }
}

void Merger::LoadRemainder(Module &MergedModule, std::unique_ptr<Module> M,
                           std::vector<GlobalItem *> &GIs) {
  ExitOnError Err("Merger::LoadRemainder: ");

  // Make all globals external so function modules can link to them.
  for (GlobalObject &GO : M->global_objects()) {
    LinkageMap[&GO] = GO.getLinkage();
    GO.setLinkage(GlobalValue::ExternalLinkage);
  }

  std::vector<GlobalValue *> ValuesToLink;
  for (GlobalItem *GI : GIs) {
    GlobalValue *GV = M->getNamedValue(GI->NewName);
    ValuesToLink.push_back(GV);
  }

  IRMover Mover(MergedModule); // TODO: don't recreate every time
  Err(Mover.move(
      std::move(M), ValuesToLink,
      [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
      /* LinkModuleInlineAsm */ false,
#endif
      /* IsPerformingImport */ false));
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
        if (Res.Module.empty()) {
          // reserve the name for dynamic linking
          Merger->ReservedNames.insert(Res.Name);
        } else {
          auto *GV = Merger->ModRemainders[Res.Module]->getNamedValue(Res.Name);
          assert(GV && Merger->GlobalItems.count(GV));
          item.second.RefItems.push_back(&Merger->GlobalItems[GV]);
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
  auto ItemComp = [](GlobalItem *a, GlobalItem *b) {
    return a->PartID < b->PartID ||
           (a->PartID == b->PartID && a->Refs < b->Refs);
  };
  auto GroupComp = [&](const Group &a, const Group &b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        ItemComp);
  };
  std::set<Group, decltype(GroupComp)> Groups(GroupComp);
  for (auto &const_SCC : make_range(scc_begin(&Graph), scc_end(&Graph))) {
    if (const_SCC.size() == 1 && const_SCC[0] == &Graph.Root)
      continue;
    Group SCC(const_SCC.begin(), const_SCC.end());
    bool CanMerge = true;
    for (GlobalItem *Item : SCC)
      if (Item->PartID.empty())
        CanMerge = false;
    if (CanMerge) {
      std::sort(SCC.begin(), SCC.end(), ItemComp);
      auto Inserted = Groups.insert(SCC);
      if (!Inserted.second)
        for (const auto &Tuple : zip_first(SCC, *Inserted.first))
          std::get<0>(Tuple)->NewDefName = std::get<1>(Tuple)->NewDefName;
    }
    for (GlobalItem *Item : SCC) {
      if (!Item->PartID.empty() && Item->NewDefName.empty())
        Item->NewDefName = ReserveName("__bcdb_id_" + Item->PartID);
      if (Item->NewName.empty())
        Item->NewName = ReserveName(Item->Name);
    }
  }
  if (WriteGlobalGraph)
    WriteGraph(&Graph, "merger_global_graph");
}

std::unique_ptr<Module> Merger::Finish() {
  auto MergedModule = std::make_unique<Module>("merged", bcdb.GetContext());
  for (auto &MR : ModRemainders) {
    StringRef ModuleName = MR.first();
    std::unique_ptr<Module> &M = MR.second;

    std::vector<GlobalItem *> GIs;
    std::map<std::string, ResolvedReference> Refs;
    for (auto &GV :
         concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
      if (!GV.isDeclaration()) {
        GlobalItem &GI = GlobalItems[&GV];
        if (!GI.PartID.empty()) {
          GlobalValue *Def = LoadPartDefinition(*MergedModule, GI);
          AddPartStub(*MergedModule, GI, Def, &GV);
        } else {
          // FIXME: what if refs to a definition in the remainder are resolved
          // to something else?
          Refs[GV.getName()] = ResolvedReference(ModuleName, GV.getName());
          GIs.push_back(&GI);
          for (auto &Item : GI.Refs)
            Refs[Item.first] = Item.second;
        }
      }
    }

    ApplyNewNames(*M, Refs);
    LoadRemainder(*MergedModule, std::move(M), GIs);
  }

  for (auto Item : LinkageMap)
    Item.first->setLinkage(Item.second);

  return MergedModule;
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
    return ResolvedReference(ModuleName, Name);
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
