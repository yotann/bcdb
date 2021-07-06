#include "bcdb/Outlining/Dependence.h"

#include <llvm/ADT/SparseBitVector.h>
#include <llvm/Analysis/MemorySSA.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/FormattedStream.h>
#include <vector>

#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

namespace {
class OutliningDependenceWriter : public AssemblyAnnotationWriter {
  const OutliningDependenceResults *OutDep;

  void emitValueAnnot(const char *type, const Value *V,
                      formatted_raw_ostream &OS) {
    auto II = OutDep->NodeIndices.find(V);
    if (II == OutDep->NodeIndices.end())
      return;
    int i = II->second;
    OS << formatv("; {0} {1} ", type, i);
    if (OutDep->PreventsOutlining.test(i)) {
      OS << "prevents outlining\n";
    } else {
      OS << "depends [";
      bool first = true;
      for (auto j : OutDep->DominatingDepends[i]) {
        if (!first)
          OS << ", ";
        first = false;
        OS << j;
      }
      OS << "] forced [";
      first = true;
      for (auto j : OutDep->ForcedDepends[i]) {
        if (!first)
          OS << ", ";
        first = false;
        OS << j;
      }
      OS << "]\n";
    }
  }

public:
  OutliningDependenceWriter(const OutliningDependenceResults *OutDep)
      : OutDep(OutDep) {}

  void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                formatted_raw_ostream &OS) override {
    emitValueAnnot("block", BB, OS);
    MemoryPhi *MPhi = OutDep->MSSA.getMemoryAccess(BB);
    if (MPhi)
      emitValueAnnot("memoryphi", MPhi, OS);
  }

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override {
    emitValueAnnot("node", I, OS);
  }
};
} // end anonymous namespace

OutliningDependenceResults::OutliningDependenceResults(Function &F,
                                                       DominatorTree &DT,
                                                       PostDominatorTree &PDT,
                                                       MemorySSA &MSSA)
    : F(F), DT(DT), PDT(PDT), MSSA(MSSA) {
  DT.updateDFSNumbers();  // Needed for fast queries.
  PDT.updateDFSNumbers(); // Needed for fast queries.
  numberNodes();
  for (Value *V : Nodes) {
    if (BasicBlock *BB = dyn_cast<BasicBlock>(V))
      analyzeBlock(BB);
    else if (MemoryPhi *MPhi = dyn_cast<MemoryPhi>(V))
      analyzeMemoryPhi(MPhi);
    else if (Instruction *I = dyn_cast<Instruction>(V))
      analyzeInstruction(I);
    else
      llvm_unreachable("Impossible node type.");
  }
  finalizeDepends();
}

void OutliningDependenceResults::print(raw_ostream &OS) const {
  OutliningDependenceWriter Writer(this);
  F.print(OS, &Writer);
}

bool OutliningDependenceResults::isOutlinable(
    const SparseBitVector<> &BV) const {
  if (!BV.empty() && static_cast<size_t>(BV.find_last()) >= Nodes.size())
    return false;
  int OP = BV.find_first();
  if (OP < 0)
    return false;
  auto BVAndDominators = BV | Dominators[OP];
  for (auto x : BV) {
    if (!Dominators[x].test(OP))
      return false; // outlining point does not dominate all nodes
    if (!BV.contains(ForcedDepends[x]))
      return false; // forced dependency not satisfied
    if (!BVAndDominators.contains(DominatingDepends[x]))
      return false; // dominating dependency not satisfied
  }
  return true;
}

std::optional<size_t> OutliningDependenceResults::lookupNode(Value *V) {
  auto It = NodeIndices.find(V);
  if (It != NodeIndices.end())
    return It->second;
  if (MemoryAccess *MA = dyn_cast<MemoryAccess>(V)) {
    if (MSSA.isLiveOnEntryDef(MA))
      return std::nullopt;
    if (MemoryUseOrDef *MUOD = dyn_cast<MemoryUseOrDef>(MA))
      return lookupNode(MUOD->getMemoryInst());
  }
  return std::nullopt;
}

void OutliningDependenceResults::addDepend(Value *Def, Value *User,
                                           bool is_data_dependency) {
  auto UserI = lookupNode(User);
  if (!UserI)
    return;
  if (Argument *A = dyn_cast<Argument>(Def))
    ArgDepends[*UserI].set(A->getArgNo());
  auto DefI = lookupNode(Def);
  if (!DefI)
    return;

  if (Dominators[*UserI].test(*DefI))
    DominatingDepends[*UserI].set(*DefI);
  else
    ForcedDepends[*UserI].set(*DefI);

  if (is_data_dependency)
    DataDepends[*UserI].set(*DefI);
}

void OutliningDependenceResults::addForcedDepend(Value *Def, Value *User) {
  auto UserI = lookupNode(User);
  auto DefI = lookupNode(Def);
  if (!UserI || !DefI)
    return;
  ForcedDepends[*UserI].set(*DefI);
}

void OutliningDependenceResults::numberNodes() {
  // Order all the blocks so that dominators come before dominatees.
  // But try to preserve the existing order when possible, to make debugging
  // easier.
  SmallVector<BasicBlock *, 8> Blocks;
  SmallPtrSet<BasicBlock *, 8> VisitedBlocks;
  auto VisitBlock = [&](BasicBlock *BB, auto &Recurse) {
    if (!BB || !DT[BB] || !PDT[BB])
      return; // Ignore blocks that are known unreachable.
    if (VisitedBlocks.insert(BB).second) {
      auto IDom = DT[BB]->getIDom();
      if (IDom)
        Recurse(IDom->getBlock(), Recurse);
      Blocks.push_back(BB);
    }
  };
  for (BasicBlock &BB : F)
    VisitBlock(&BB, VisitBlock);

  // Create all the nodes.
  for (BasicBlock *BB : Blocks) {
    Nodes.push_back(BB);
    if (MemoryPhi *MPhi = MSSA.getMemoryAccess(BB))
      Nodes.push_back(MPhi);
    for (Instruction &I : *BB)
      Nodes.push_back(&I);
  }

  // Initialize NodeIndices and other fields.
  for (size_t i = 0; i < Nodes.size(); i++)
    NodeIndices[Nodes[i]] = i;
  Dominators.resize(Nodes.size());
  DominatingDepends.resize(Nodes.size());
  ForcedDepends.resize(Nodes.size());
  DataDepends.resize(Nodes.size());
  ArgDepends.resize(Nodes.size());

  // Fill in Dominators.
  for (size_t i = 0; i < Nodes.size(); i++) {
    Value *V = Nodes[i];
    if (BasicBlock *BB = dyn_cast<BasicBlock>(V)) {
      if (auto IDom = DT[BB]->getIDom())
        Dominators[i] =
            Dominators[NodeIndices[IDom->getBlock()->getTerminator()]];
    } else {
      // Inherit dominators from the previous node.
      Dominators[i] = Dominators[i - 1];
    }
    // Each node dominates itself.
    Dominators[i].set(i);
  }
}

void OutliningDependenceResults::analyzeBlock(BasicBlock *BB) {
  // Calculate control dependence as described in section 3.1 of:
  // J. Ferrante et al., "Program Dependence Graph and Its Use in
  // Optimization". We use the same variable names as that paper.
  BasicBlock *a = BB;
  for (BasicBlock *b : successors(a)) {
    if (PDT.properlyDominates(b, a))
      continue;

    auto l = PDT[a]->getIDom();
    SmallVector<BasicBlock *, 8> path;
    for (auto m = PDT[b]; m != l; m = m->getIDom())
      path.push_back(m->getBlock());

    // Instead of always making every node in the path control-dependent on A,
    // like in the Ferrante paper, we make an exception. If M is
    // control-dependent on A purely because one of M's dominators is
    // control-dependent on A, we ignore the dependence of M on A; instead, we
    // mark M as control-dependent on the dominator. This is useful in a case
    // like the following:
    //
    // do { a=A(); if(a) { B(); }; c=C(); } while(c);
    //
    // Normally blocks A, B, and C would all be control-dependent on C,
    // preventing us from outlining any part of the loop body; we could only
    // outline the loop as a whole. Instead, we make only A control-dependent
    // on C, and we make B and C control-dependent on A, so we aren't forced to
    // outline the whole loop if we just want to outline part of the body.
    for (size_t i = 0; i < path.size(); i++) {
      Value *dep = a->getTerminator();
      for (size_t j = 0; j < i; j++)
        if (DT.dominates(path[j], path[i]))
          dep = path[j];
      addDepend(dep, path[i]);
    }

    // If we outline a conditional branch, we must also outline every node
    // that's control-dependent on the branch (using the standard definition of
    // control dependence, not the modified one used above). Otherwise we would
    // end up needing the same conditional branch in both the outlined callee
    // and the modified caller.
    //
    // TODO: Experiment with relaxing this restriction; if the outlined callee
    // returns a value that indicates which path it took, we can duplicate the
    // conditional branches in both the caller and callee and make the caller
    // follow the same path. This still wouldn't work well for complex control
    // flow (if there's a loop containing an if-else, there's no efficient way
    // to indicate the entire path through the loop).
    Value *branch = a->getTerminator();
    for (BasicBlock *m : path) {
      addForcedDepend(m, branch);
      for (Instruction &ins : *m)
        addForcedDepend(&ins, branch);
    }
  }

  // Don't outline computed goto targets. We could outline computed gotos if we
  // also outlined every possible target of the goto, but computed gotos are
  // too rare to make it worth the trouble.
  if (BB->hasAddressTaken())
    PreventsOutlining.set(NodeIndices[BB]);
}

void OutliningDependenceResults::analyzeMemoryPhi(MemoryPhi *MPhi) {
  for (Value *V2 : MPhi->incoming_values())
    addDepend(V2, MPhi);
}

void OutliningDependenceResults::analyzeInstruction(Instruction *I) {
  // Data dependencies
  for (Value *Op : I->operands()) {
    if (Op->getType()->isTokenTy()) {
      // We can't pass a token as an argument or return value.
      addForcedDepend(Op, I);
      addForcedDepend(I, Op);
    } else if (!isa<BasicBlock>(Op)) {
      addDepend(Op, I, /* data */ true);
    }
  }

  // The value of a PHI node depends on which branch led to it.
  // TODO: if the PHI assigns the same value to multiple edges, we could
  // simplify the dependencies in some cases.
  if (PHINode *PHI = dyn_cast<PHINode>(I))
    for (BasicBlock *BB : PHI->blocks())
      addForcedDepend(BB->getTerminator(), PHI);

  // Memory dependencies
  if (MSSA.getMemoryAccess(I)) {
    // FIXME: should we be using getSkipSelfWalker?
    MemoryAccess *MA = MSSA.getWalker()->getClobberingMemoryAccess(I);
    addDepend(MA, I);
  }

  // Control dependencies
  addDepend(I->getParent(), I);

  if (I->isEHPad())
    for (BasicBlock *BB : predecessors(I->getParent()))
      addForcedDepend(BB->getTerminator(), I);
  // FIXME: is "unwind to caller" handled correctly?

  // TODO: relax some of these restrictions.

  bool prevent_all_outlining = false;
  for (Argument &Arg : F.args())
    if (Arg.hasSwiftErrorAttr())
      prevent_all_outlining = true; // paranoid
  if (AllocaInst *AI = dyn_cast<AllocaInst>(I))
    if (AI->isSwiftError())
      prevent_all_outlining = true; // paranoid

  // TODO: use PointerMayBeCaptured and mark some allocas as outlineable.
  switch (I->getOpcode()) {
  case Instruction::Alloca:
  case Instruction::IndirectBr:
  case Instruction::CallBr:
    PreventsOutlining.set(NodeIndices[I]);
    break;
  case Instruction::Invoke:
    // TODO: we could outline invoke instructions as call instructions, and
    // leave the exception handling in the caller.
    addForcedDepend(cast<InvokeInst>(I)->getUnwindDest()->getFirstNonPHI(), I);
    break;
  case Instruction::CleanupRet:
  case Instruction::CatchSwitch:
    for (BasicBlock *Succ : successors(I->getParent()))
      addForcedDepend(Succ->getFirstNonPHI(), I);
    break;
  default:
    break;
  }

  if (CallBase *CB = dyn_cast<CallBase>(I)) {
    if (CB->isMustTailCall())
      PreventsOutlining.set(NodeIndices[I]);

    switch (CB->getIntrinsicID()) {
    case Intrinsic::addressofreturnaddress:
    case Intrinsic::frameaddress:
    case Intrinsic::lifetime_end:
    case Intrinsic::lifetime_start:
    case Intrinsic::returnaddress:
    case Intrinsic::vaend:
    case Intrinsic::vastart:
      PreventsOutlining.set(NodeIndices[I]);
      break;

    case Intrinsic::annotation:
    case Intrinsic::codeview_annotation:
    case Intrinsic::dbg_addr:
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_label:
    case Intrinsic::dbg_value:
    case Intrinsic::eh_dwarf_cfa:
    case Intrinsic::eh_exceptioncode:
    case Intrinsic::eh_recoverfp:
    case Intrinsic::eh_return_i32:
    case Intrinsic::eh_return_i64:
    case Intrinsic::eh_sjlj_callsite:
    case Intrinsic::eh_sjlj_functioncontext:
    case Intrinsic::eh_sjlj_longjmp:
    case Intrinsic::eh_sjlj_lsda:
    case Intrinsic::eh_sjlj_setjmp:
    case Intrinsic::eh_sjlj_setup_dispatch:
    case Intrinsic::eh_typeid_for:
    case Intrinsic::eh_unwind_init:
    case Intrinsic::experimental_deoptimize:
    case Intrinsic::experimental_gc_relocate:
    case Intrinsic::experimental_gc_result:
    case Intrinsic::experimental_gc_statepoint:
    case Intrinsic::experimental_guard:
    case Intrinsic::gcread:
    case Intrinsic::gcroot:
    case Intrinsic::gcwrite:
    case Intrinsic::get_dynamic_area_offset:
    case Intrinsic::invariant_end:
    case Intrinsic::invariant_start:
    case Intrinsic::launder_invariant_group:
    case Intrinsic::localaddress:
    case Intrinsic::localescape:
    case Intrinsic::localrecover:
    case Intrinsic::ptr_annotation:
    case Intrinsic::sponentry:
    case Intrinsic::stackguard:
    case Intrinsic::stackprotector:
    case Intrinsic::stackrestore:
    case Intrinsic::stacksave:
    case Intrinsic::strip_invariant_group:
    case Intrinsic::var_annotation:
      prevent_all_outlining = true; // paranoid
      break;
    default:
      break;
    }
  }

  if (prevent_all_outlining)
    for (size_t i = 0; i < Nodes.size(); i++)
      PreventsOutlining.set(i);
}

void OutliningDependenceResults::getExternals(
    const SparseBitVector<> &BV, SparseBitVector<> &ArgInputs,
    SparseBitVector<> &ExternalInputs, SparseBitVector<> &ExternalOutputs) {
  // Figure out which inputs the outlined function will need to take as
  // arguments.
  ArgInputs.clear();
  ExternalInputs.clear();
  ExternalOutputs.clear();
  for (auto i : BV) {
    // TODO: phi nodes could be handled better in certain cases. If the
    // outlining point is a block header, a phi node may have multiple inputs
    // from non-included blocks. The outlined function will really only need
    // one argument to handle all of those inputs.
    ExternalInputs |= DataDepends[i];
    ArgInputs |= ArgDepends[i];
  }
  ExternalInputs.intersectWithComplement(BV);

  // Figure out which outputs the outlined function will need to include in its
  // return value.
  for (size_t i = 0; i < Nodes.size(); i++) {
    if (BV.test(i))
      continue;
    ExternalOutputs |= DataDepends[i];
  }
  ExternalOutputs &= BV;
}

void OutliningDependenceResults::finalizeDepends() {
  // Make DominatingDepends transitive.
  for (size_t i = 0; i < Nodes.size(); i++)
    for (auto x : DominatingDepends[i])
      DominatingDepends[i] |= DominatingDepends[x];

  // If a phi node is outlined, force the block header to be outlined too.
  // (It doesn't make sense to outline a phi node without the other phi nodes,
  // or without some control flow leading to the block.)
  size_t HeaderI = 0;
  for (size_t i = 0; i < Nodes.size(); i++) {
    if (isa<BasicBlock>(Nodes[i]))
      HeaderI = i;
    if (isa<PHINode>(Nodes[i]) || isa<MemoryPhi>(Nodes[i]))
      ForcedDepends[i].set(HeaderI);
  }

  // Make ForcedDepends[i] include everything we need to outline in order to
  // outline Nodes[i]. We add several things to ForcedDepends[i]:
  //
  // A. A node that dominates i and also dominates x for each x in
  //    ForcedDepends[i]. This node may be i itself. Ensures we have a valid
  //    outlining point.
  //
  // B. ForcedDepends[x] for each x in ForcedDepends[i]. Ensures ForcedDepends
  //    is transitive.
  //
  // C. DominatingDepends[x] for each x in ForcedDepends[i], excluding nodes
  //    that dominate the outlining point from part A. We need to outline these
  //    nodes in order to use the chosen outlining point.
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < Nodes.size(); i++) {
      if (ForcedDepends[i].empty())
        continue;

      SparseBitVector OldForcedDepends = ForcedDepends[i];
      SparseBitVector Doms = Dominators[i];
      SparseBitVector Deps = DominatingDepends[i];
      for (auto x : ForcedDepends[i]) {
        ForcedDepends[i] |= ForcedDepends[x];
        Doms &= Dominators[x];
        Deps |= DominatingDepends[x];
      }
      Deps.intersectWithComplement(Doms);
      ForcedDepends[i] |= Deps;
      ForcedDepends[i].set(Doms.find_last());
      if (ForcedDepends[i] != OldForcedDepends)
        changed = true;
    }
  }
}

OutliningDependenceWrapperPass::OutliningDependenceWrapperPass()
    : FunctionPass(ID) {}

bool OutliningDependenceWrapperPass::runOnFunction(Function &F) {
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &PDT = getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
  auto &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();
  OutDep.emplace(F, DT, PDT, MSSA);
  return false;
}

void OutliningDependenceWrapperPass::print(raw_ostream &OS,
                                           const Module *M) const {
  OutDep->print(OS);
}

void OutliningDependenceWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<PostDominatorTreeWrapperPass>();
  AU.addRequiredTransitive<MemorySSAWrapperPass>();
}

void OutliningDependenceWrapperPass::releaseMemory() { OutDep.reset(); }

void OutliningDependenceWrapperPass::verifyAnalysis() const {
  assert(false && "unimplemented");
}

char OutliningDependenceWrapperPass::ID = 0;
static RegisterPass<OutliningDependenceWrapperPass>
    X("outlining-dependence", "Outlining Dependence Analysis Pass", false,
      true);
