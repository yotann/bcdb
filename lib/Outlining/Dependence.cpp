#include "Outlining/Dependence.h"

#include <llvm/ADT/SparseBitVector.h>
#include <llvm/Analysis/CaptureTracking.h>
#include <llvm/Analysis/MemorySSA.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/FormattedStream.h>
#include <vector>

#include "Outlining/CorrectPostDominatorTree.h"
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
    OS << formatv("; {0} {1}", type, i);
    const auto &arg = OutDep->ArgDepends[i];
    const auto &data = OutDep->DataDepends[i];
    const auto &dominating = OutDep->DominatingDepends[i];
    const auto &forced = OutDep->ForcedDepends[i];
    if (!arg.empty()) {
      OS << " arg [";
      OutDep->printSet(OS, arg);
      OS << "]";
    }
    if (!data.empty()) {
      OS << " data [";
      OutDep->printSet(OS, data);
      OS << "]";
    }
    if (!dominating.empty()) {
      OS << " dominating [";
      OutDep->printSet(OS, dominating);
      OS << "]";
    }
    if (!forced.empty()) {
      OS << " forced [";
      OutDep->printSet(OS, forced);
      OS << "]";
    }
    if (OutDep->PreventsOutlining.test(i))
      OS << " prevents outlining";
    OS << "\n";
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
  CPDT = std::make_unique<CorrectPostDominatorTree>(F);
  DT.updateDFSNumbers();    // Needed for fast queries.
  PDT.updateDFSNumbers();   // Needed for fast queries.
  CPDT->updateDFSNumbers(); // Needed for fast queries.
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
  if (BV.empty())
    return false;
  if (static_cast<size_t>(BV.find_last()) >= Nodes.size())
    return false;
  if (BV.intersects(PreventsOutlining))
    return false;
  unsigned OP = static_cast<unsigned>(BV.find_first());
  auto BVAndDominators = BV | Dominators[OP];
  for (auto i : BV) {
    if (!Dominators[i].test(OP))
      return false; // outlining point does not dominate all nodes
    if (!BV.contains(ForcedDepends[i]))
      return false; // forced dependency not satisfied
    if (!BVAndDominators.contains(DominatingDepends[i]))
      return false; // dominating dependency not satisfied
  }
  return true;
}

void OutliningDependenceResults::printSet(raw_ostream &os,
                                          const SparseBitVector<> &bv,
                                          llvm::StringRef sep,
                                          llvm::StringRef range) const {
  size_t last_end = 0;
  for (auto start : bv) {
    if (start < last_end)
      continue;
    auto end = start + 1;
    while (bv.test(end))
      end++;
    if (last_end != 0)
      os << sep;
    last_end = end;
    if (start + 1 == end)
      os << formatv("{0}", start);
    else
      os << formatv("{0}{1}{2}", start, range, end - 1);
  }
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

void OutliningDependenceResults::addDepend(Value *User, Value *Def,
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

void OutliningDependenceResults::addForcedDepend(Value *User, Value *Def) {
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
    if (VisitedBlocks.insert(BB).second) {
      if (!BB || !DT[BB] || !PDT[BB])
        return; // Ignore blocks that are known unreachable.
      if (auto IDom = DT[BB]->getIDom())
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

namespace {
struct RecordingCaptureTracker : CaptureTracker {
  SmallVector<Instruction *, 20> uses;
  bool Captured = false;

  void tooManyUses() override { Captured = true; }

  bool shouldExplore(const Use *use) override {
    uses.push_back(cast<Instruction>(use->getUser()));
    return true;
  }

  bool captured(const Use *use) override {
    Captured = true;
    return true;
  }
};
} // end anonymous namespace

void OutliningDependenceResults::analyzeBlock(BasicBlock *BB) {
  // Calculate control dependence as described in section 3.1 of:
  // J. Ferrante et al., "Program Dependence Graph and Its Use in
  // Optimization". We use the same variable names as that paper.

  // This loop finds all control dependences and adds corresponding dominating
  // dependences.
  //
  // We can't use LLVM's standard PostDominatorTree because it ignores implicit
  // control dependences in cases like this:
  //
  // function_that_sometimes_calls_abort();
  // if (condition) { /* ... */ }
  // puts("success");
  //
  // The puts() has a control dependence on
  // function_that_sometimes_calls_abort(), but PostDominatorTree ignores the
  // dependence. We use CorrectPostDominatorTree instead, which handles the
  // dependence properly by adding an implicit CFG node with edges coming from
  // every block that can throw or abort.
  //
  // Note that this loop doesn't need to iterate over the new CFG edges added
  // by CorrectPostDominatorTree. (They only affect the dependences of the
  // implicit node, which we don't care about.)
  BasicBlock *a = BB;
  for (BasicBlock *b : successors(a)) {
    if (CPDT->properlyDominates(b, a))
      continue;

    auto l = (*CPDT)[a]->getIDom();
    SmallVector<BasicBlock *, 8> path;
    for (auto m = (*CPDT)[b]; m != l; m = m->getIDom())
      path.push_back(m->getBlock()->bb);

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
      if (!path[i]) {
        // We reached the implicit CFG node added by CorrectPostDominatorTree,
        // which doesn't have a dependence node.
        assert(i == path.size() - 1);
        break;
      }
      Value *dep = a->getTerminator();
      for (size_t j = 0; j < i; j++)
        if (DT.dominates(path[j], path[i]))
          dep = path[j];
      addDepend(path[i], dep);
    }
  }

  // If we outline a conditional branch, we must also outline every node that's
  // control-dependent on the branch. Otherwise we would end up needing the
  // same conditional branch in both the outlined callee and the modified
  // caller.
  //
  // This loop needs to use the standard PostDominatorTree, not
  // CorrectPostDominatorTree, because we only care about explicit control
  // flow. If we outline a call like function_that_sometimes_calls_abort(),
  // without the other statements that have an implicit control dependence on
  // it, the program will still work fine.
  //
  // TODO: Experiment with relaxing this restriction; if the outlined callee
  // returns a value that indicates which path it took, we can duplicate the
  // conditional branches in both the caller and callee and make the caller
  // follow the same path. This still wouldn't work well for complex control
  // flow (if there's a loop containing an if-else, there's no efficient way to
  // indicate the entire path through the loop).
  a = BB;
  for (BasicBlock *b : successors(a)) {
    if (PDT.properlyDominates(b, a))
      continue;

    auto l = PDT[a]->getIDom();
    SmallVector<BasicBlock *, 8> path;
    for (auto m = PDT[b]; m != l; m = m->getIDom())
      path.push_back(m->getBlock());

    Value *branch = a->getTerminator();
    for (BasicBlock *m : path) {
      addForcedDepend(branch, m);
      for (Instruction &ins : *m)
        addForcedDepend(branch, &ins);
    }
  }

  // Don't outline computed goto targets. We could outline computed gotos if we
  // also outlined every possible target of the goto, but computed gotos are
  // too rare to make it worth the trouble.
  if (BB->hasAddressTaken())
    PreventsOutlining.set(NodeIndices[BB]);
}

void OutliningDependenceResults::analyzeMemoryPhi(MemoryPhi *MPhi) {
  addForcedDepend(MPhi, MPhi->getBlock());
  for (Value *value : MPhi->incoming_values())
    addDepend(MPhi, value);

  // This is probably redundant, but let's keep it so we match the handling of
  // PHINode.
  for (BasicBlock *block : MPhi->blocks())
    addDepend(MPhi, block->getTerminator());
}

void OutliningDependenceResults::analyzeInstruction(Instruction *I) {
  // Data dependences.
  for (Value *Op : I->operands()) {
    if (Op->getType()->isTokenTy()) {
      // We can't pass a token as an argument or return value.
      addForcedDepend(Op, I);
      addForcedDepend(I, Op);
    } else if (!isa<BasicBlock>(Op)) {
      addDepend(I, Op, /*is_data_dependency*/ true);
    }
  }

  // Memory dependences.
  if (MSSA.getMemoryAccess(I)) {
    MemoryAccess *MA = MSSA.getWalker()->getClobberingMemoryAccess(I);
    addDepend(I, MA);
  }

  // Control dependences.
  addDepend(I, I->getParent());

  // Implicit control dependences within the same block.
  // (For implicit control dependences that cross multiple blocks, the
  // depending instruction will depend on its own block thanks to the line
  // above, which will depend on this instruction's block's terminator thanks
  // to analyzeBlock(), which will depend on this instruction thanks to the
  // following code.)
  if (!isGuaranteedToTransferExecutionToSuccessor(I)) {
    // All subsequent instructions are actually control-dependent on this one.
    for (Instruction *J = I->getNextNode(); J != nullptr; J = J->getNextNode())
      addDepend(J, I);
  }

  // Exception pad instructions must stay with their predecessors.
  if (I->isEHPad())
    for (BasicBlock *BB : predecessors(I->getParent()))
      addForcedDepend(I, BB->getTerminator());

  switch (I->getOpcode()) {
  case Instruction::PHI:
    // Only outline PHI nodes when also outlining the full basic block head.
    addForcedDepend(I, I->getParent());
    // We can only calculate the PHI value correctly if we know the control
    // flow leading to it.
    for (BasicBlock *BB : cast<PHINode>(I)->blocks())
      addDepend(I, BB->getTerminator());
    break;
  case Instruction::Ret:
    // TODO: if we're returning a value other than void, we could outline the
    // return instructions as long as every path in the outlined callee reaches
    // a return instruction (or unreachable, or a call to abort(), etc.). It
    // might help to use the mergereturn pass first.
    PreventsOutlining.set(NodeIndices[I]);
    break;
  case Instruction::IndirectBr:
  case Instruction::CallBr:
    // Too rare to bother adding outlining support.
    PreventsOutlining.set(NodeIndices[I]);
    break;
  case Instruction::Invoke:
    // TODO: we could outline invoke instructions as call instructions, and
    // leave the exception handling in the caller.
    addForcedDepend(I, cast<InvokeInst>(I)->getUnwindDest()->getFirstNonPHI());
    break;
  case Instruction::Resume:
    // landingpad and resume instructions must stay together.
    for (BasicBlock &BB : F) {
      Instruction *first = BB.getFirstNonPHI();
      if (first->getOpcode() == Instruction::LandingPad) {
        addForcedDepend(I, first);
        addForcedDepend(first, I);
      }
    }
    break;
  case Instruction::CatchSwitch:
  case Instruction::CleanupRet:
    if (auto cri = dyn_cast<CleanupReturnInst>(I))
      if (cri->unwindsToCaller())
        PreventsOutlining.set(NodeIndices[I]); // same as a return instruction
    if (auto csi = dyn_cast<CatchSwitchInst>(I))
      if (csi->unwindsToCaller())
        PreventsOutlining.set(NodeIndices[I]); // same as a return instruction
    // These instructions must stay together with their successors.
    for (BasicBlock *Succ : successors(I->getParent()))
      addForcedDepend(I, Succ->getFirstNonPHI());
    break;
  case Instruction::Alloca: {
    // If the alloca is outlined, everything that could possibly use it must be
    // outlined too.
    RecordingCaptureTracker tracker;
    PointerMayBeCaptured(I, &tracker, /*MaxUsesToExplore*/ 100);
    if (tracker.Captured) {
      // We can't tell which instructions might use the alloca, so we can't
      // outline it.
      PreventsOutlining.set(NodeIndices[I]);
    } else {
      for (Instruction *use : tracker.uses)
        addForcedDepend(I, use);
    }
    break;
  }
  default:
    break;
  }

  if (CallBase *CB = dyn_cast<CallBase>(I)) {
    if (CB->isMustTailCall())
      PreventsOutlining.set(NodeIndices[I]);

    // Some intrinsics, like vastart, will not work correctly if moved to
    // another function. Unfortunately there's no simple way to check which
    // intrinsics are outlinable, so we need to list all the safe ones.
    switch (CB->getIntrinsicID()) {

    case Intrinsic::not_intrinsic:
      // Safe to outline.
      CompilesToCall.set(NodeIndices[I]);
      break;

    case Intrinsic::bitreverse:
    case Intrinsic::bswap:
    case Intrinsic::canonicalize:
    case Intrinsic::ceil:
    case Intrinsic::convert_from_fp16:
    case Intrinsic::convert_to_fp16:
    case Intrinsic::copysign:
    case Intrinsic::cos:
    case Intrinsic::ctlz:
    case Intrinsic::ctpop:
    case Intrinsic::cttz:
    case Intrinsic::exp:
    case Intrinsic::exp2:
    case Intrinsic::fabs:
    case Intrinsic::floor:
    case Intrinsic::fma:
    case Intrinsic::fmuladd:
    case Intrinsic::fshl:
    case Intrinsic::fshr:
    case Intrinsic::llrint:
    case Intrinsic::llround:
    case Intrinsic::log:
    case Intrinsic::log10:
    case Intrinsic::log2:
    case Intrinsic::lrint:
    case Intrinsic::lround:
    case Intrinsic::maximum:
    case Intrinsic::maxnum:
    case Intrinsic::minimum:
    case Intrinsic::minnum:
    case Intrinsic::nearbyint:
    case Intrinsic::pow:
    case Intrinsic::powi:
    case Intrinsic::rint:
    case Intrinsic::round:
    case Intrinsic::sadd_sat:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::sdiv_fix:
    case Intrinsic::sin:
    case Intrinsic::smul_fix:
    case Intrinsic::smul_fix_sat:
    case Intrinsic::smul_with_overflow:
    case Intrinsic::sqrt:
    case Intrinsic::ssub_sat:
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::trunc:
    case Intrinsic::uadd_sat:
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::udiv_fix:
    case Intrinsic::umul_fix:
    case Intrinsic::umul_fix_sat:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::usub_sat:
    case Intrinsic::usub_with_overflow:
#if LLVM_VERSION_MAJOR >= 11
    case Intrinsic::roundeven:
    case Intrinsic::sdiv_fix_sat:
    case Intrinsic::udiv_fix_sat:
    case Intrinsic::vp_add:
    case Intrinsic::vp_and:
    case Intrinsic::vp_ashr:
    case Intrinsic::vp_lshr:
    case Intrinsic::vp_mul:
    case Intrinsic::vp_or:
    case Intrinsic::vp_sdiv:
    case Intrinsic::vp_shl:
    case Intrinsic::vp_srem:
    case Intrinsic::vp_sub:
    case Intrinsic::vp_udiv:
    case Intrinsic::vp_urem:
    case Intrinsic::vp_xor:
#endif
#if LLVM_VERSION_MAJOR >= 12
    case Intrinsic::abs:
    case Intrinsic::fptosi_sat:
    case Intrinsic::fptoui_sat:
    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::sshl_sat:
    case Intrinsic::umax:
    case Intrinsic::umin:
    case Intrinsic::ushl_sat:
    case Intrinsic::vector_reduce_add:
    case Intrinsic::vector_reduce_and:
    case Intrinsic::vector_reduce_fadd:
    case Intrinsic::vector_reduce_fmax:
    case Intrinsic::vector_reduce_fmin:
    case Intrinsic::vector_reduce_fmul:
    case Intrinsic::vector_reduce_mul:
    case Intrinsic::vector_reduce_or:
    case Intrinsic::vector_reduce_smax:
    case Intrinsic::vector_reduce_smin:
    case Intrinsic::vector_reduce_umax:
    case Intrinsic::vector_reduce_umin:
    case Intrinsic::vector_reduce_xor:
#endif
      // Simple computations (may depend on rounding mode). Safe to outline.
      // TODO: Some of these should have CompilesToCall set.
      break;

    case Intrinsic::expect:
    case Intrinsic::lifetime_end:
    case Intrinsic::vacopy:
#if LLVM_VERSION_MAJOR >= 11
    case Intrinsic::expect_with_probability:
    case Intrinsic::memcpy_inline:
#endif
      // Should be safe to outline.
      break;

    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memset:
      // Should be safe to outline.
      CompilesToCall.set(NodeIndices[I]);
      break;

    case Intrinsic::lifetime_start:
      // Can't necessarily be outlined. If there are two
      // lifetime_start...lifetime_end ranges, and we outline the first one,
      // LLVM will incorrectly assume the object is dead until the start of the
      // second range.
      //
      // TODO: Try to handle this using forced dependences (reuse the code that
      // handles allocas).
      PreventsOutlining.set(NodeIndices[I]);
      break;

    case Intrinsic::addressofreturnaddress:
    case Intrinsic::eh_typeid_for:
    case Intrinsic::frameaddress:
    case Intrinsic::returnaddress:
    case Intrinsic::sponentry:
    case Intrinsic::stackprotector:
    case Intrinsic::vaend:
    case Intrinsic::vastart:
      // Inherently can't be outlined.
      PreventsOutlining.set(NodeIndices[I]);
      break;

    default:
      errs() << "note: assuming intrinsic can't be outlined: "
             << CB->getCalledFunction()->getName() << "\n";
      PreventsOutlining.set(NodeIndices[I]);
      break;
    }
  }
}

void OutliningDependenceResults::finalizeDepends() {
  // Make DominatingDepends transitive.
  for (size_t i = 0; i < Nodes.size(); i++)
    for (auto x : DominatingDepends[i])
      DominatingDepends[i] |= DominatingDepends[x];

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
  bool changed;
  do {
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
  } while (changed);

  for (size_t i = 0; i < Nodes.size(); ++i) {
    if (ForcedDepends[i].intersects(PreventsOutlining))
      PreventsOutlining.set(i);
    DominatingDepends[i].intersectWithComplement(ForcedDepends[i]);
  }

  for (auto i : PreventsOutlining) {
    // Leave DataDepends alone; it's still needed to determine return values
    // from the outlined callee.
    ArgDepends[i].clear();
    ForcedDepends[i].clear();
    DominatingDepends[i].clear();
  }
}

AnalysisKey OutliningDependenceAnalysis::Key;

OutliningDependenceResults
OutliningDependenceAnalysis::run(Function &f, FunctionAnalysisManager &am) {
  auto &dt = am.getResult<DominatorTreeAnalysis>(f);
  auto &pdt = am.getResult<PostDominatorTreeAnalysis>(f);
  auto &mssa = am.getResult<MemorySSAAnalysis>(f);
  return OutliningDependenceResults(f, dt, pdt, mssa.getMSSA());
}

PreservedAnalyses
OutliningDependencePrinterPass::run(Function &f, FunctionAnalysisManager &am) {
  auto &deps = am.getResult<OutliningDependenceAnalysis>(f);
  os << "OutliningDependence for function: " << f.getName() << "\n";
  deps.print(os);
  return PreservedAnalyses::all();
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
