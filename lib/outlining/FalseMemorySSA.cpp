// See explanation at top of FalseMemorySSA.h.
#include "outlining/FalseMemorySSA.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Use.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "false-memoryssa"

static cl::opt<unsigned> MaxCheckLimit(
    "falsememssa-check-limit", cl::Hidden, cl::init(100),
    cl::desc("The maximum number of stores/phis MemorySSA"
             "will consider trying to walk past (default = 100)"));

namespace llvm {

/// An assembly annotator class to print Memory SSA information in
/// comments.
class FalseMemorySSAAnnotatedWriter : public AssemblyAnnotationWriter {
  friend class FalseMemorySSA;

  const FalseMemorySSA *MSSA;

public:
  FalseMemorySSAAnnotatedWriter(const FalseMemorySSA *M) : MSSA(M) {}

  void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(BB))
      OS << "; " << *MA << "\n";
  }

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(I))
      OS << "; " << *MA << "\n";
  }
};

} // end namespace llvm

namespace {

/// Our current alias analysis API differentiates heavily between calls and
/// non-calls, and functions called on one usually assert on the other.
/// This class encapsulates the distinction to simplify other code that wants
/// "Memory affecting instructions and related data" to use as a key.
/// For example, this class is used as a densemap key in the use optimizer.
class MemoryLocOrCall {
public:
  bool IsCall = false;

  MemoryLocOrCall(MemoryUseOrDef *MUD)
      : MemoryLocOrCall(MUD->getMemoryInst()) {}
  MemoryLocOrCall(const MemoryUseOrDef *MUD)
      : MemoryLocOrCall(MUD->getMemoryInst()) {}

  MemoryLocOrCall(Instruction *Inst) {
    if (auto *C = dyn_cast<CallBase>(Inst)) {
      IsCall = true;
      Call = C;
    } else {
      IsCall = false;
      // There is no such thing as a memorylocation for a fence inst, and it is
      // unique in that regard.
      if (!isa<FenceInst>(Inst))
        Loc = MemoryLocation::get(Inst);
    }
  }

  explicit MemoryLocOrCall(const MemoryLocation &Loc) : Loc(Loc) {}

  const CallBase *getCall() const {
    assert(IsCall);
    return Call;
  }

  MemoryLocation getLoc() const {
    assert(!IsCall);
    return Loc;
  }

  bool operator==(const MemoryLocOrCall &Other) const {
    if (IsCall != Other.IsCall)
      return false;

    if (!IsCall)
      return Loc == Other.Loc;

    if (Call->getCalledOperand() != Other.Call->getCalledOperand())
      return false;

    return Call->arg_size() == Other.Call->arg_size() &&
           std::equal(Call->arg_begin(), Call->arg_end(),
                      Other.Call->arg_begin());
  }

private:
  union {
    const CallBase *Call;
    MemoryLocation Loc;
  };
};

} // end anonymous namespace

namespace llvm {

template <> struct DenseMapInfo<MemoryLocOrCall> {
  static inline MemoryLocOrCall getEmptyKey() {
    return MemoryLocOrCall(DenseMapInfo<MemoryLocation>::getEmptyKey());
  }

  static inline MemoryLocOrCall getTombstoneKey() {
    return MemoryLocOrCall(DenseMapInfo<MemoryLocation>::getTombstoneKey());
  }

  static unsigned getHashValue(const MemoryLocOrCall &MLOC) {
    if (!MLOC.IsCall)
      return hash_combine(
          MLOC.IsCall,
          DenseMapInfo<MemoryLocation>::getHashValue(MLOC.getLoc()));

    hash_code hash =
        hash_combine(MLOC.IsCall, DenseMapInfo<const Value *>::getHashValue(
                                      MLOC.getCall()->getCalledOperand()));

    for (const Value *Arg : MLOC.getCall()->args())
      hash = hash_combine(hash, DenseMapInfo<const Value *>::getHashValue(Arg));
    return hash;
  }

  static bool isEqual(const MemoryLocOrCall &LHS, const MemoryLocOrCall &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

/// This does one-way checks to see if Use could theoretically be hoisted above
/// MayClobber. This will not check the other way around.
///
/// This assumes that, for the purposes of MemorySSA, Use comes directly after
/// MayClobber, with no potentially clobbering operations in between them.
/// (Where potentially clobbering ops are memory barriers, aliased stores, etc.)
static bool areLoadsReorderable(const LoadInst *Use,
                                const LoadInst *MayClobber) {
  bool VolatileUse = Use->isVolatile();
  bool VolatileClobber = MayClobber->isVolatile();
  // Volatile operations may never be reordered with other volatile operations.
  if (VolatileUse && VolatileClobber)
    return false;
  // Otherwise, volatile doesn't matter here. From the language reference:
  // 'optimizers may change the order of volatile operations relative to
  // non-volatile operations.'"

  // If a load is seq_cst, it cannot be moved above other loads. If its ordering
  // is weaker, it can be moved above other loads. We just need to be sure that
  // MayClobber isn't an acquire load, because loads can't be moved above
  // acquire loads.
  //
  // Note that this explicitly *does* allow the free reordering of monotonic (or
  // weaker) loads of the same address.
  bool SeqCstUse = Use->getOrdering() == AtomicOrdering::SequentiallyConsistent;
  bool MayClobberIsAcquire = isAtLeastOrStrongerThan(MayClobber->getOrdering(),
                                                     AtomicOrdering::Acquire);
  return !(SeqCstUse || MayClobberIsAcquire);
}

namespace {

struct ClobberAlias {
  bool IsClobber;
  Optional<AliasResult> AR;
};

} // end anonymous namespace

// Return a pair of {IsClobber (bool), AR (AliasResult)}. It relies on AR being
// ignored if IsClobber = false.
template <typename AliasAnalysisType>
static ClobberAlias
instructionClobbersQuery(const MemoryDef *MD, const MemoryLocation &UseLoc,
                         const Instruction *UseInst, AliasAnalysisType &AA,
                         bool treat_loads_as_defs = true) {
  Instruction *DefInst = MD->getMemoryInst();
  assert(DefInst && "Defining instruction not actually an instruction");
  Optional<AliasResult> AR;

  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(DefInst)) {
    // These intrinsics will show up as affecting memory, but they are just
    // markers, mostly.
    //
    // FIXME: We probably don't actually want MemorySSA to model these at all
    // (including creating MemoryAccesses for them): we just end up inventing
    // clobbers where they don't really exist at all. Please see D43269 for
    // context.
    switch (II->getIntrinsicID()) {
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::assume:
#if LLVM_VERSION_MAJOR >= 12
    case Intrinsic::experimental_noalias_scope_decl:
#endif
      return {false, NoAlias};
    case Intrinsic::dbg_addr:
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_label:
    case Intrinsic::dbg_value:
      llvm_unreachable("debuginfo shouldn't have associated defs!");
    default:
      break;
    }
  }

  if (auto *CB = dyn_cast_or_null<CallBase>(UseInst)) {
    ModRefInfo I = AA.getModRefInfo(DefInst, CB);
    AR = isMustSet(I) ? MustAlias : MayAlias;
    return {isModOrRefSet(I), AR};
  }

  if (!treat_loads_as_defs)
    if (auto *DefLoad = dyn_cast<LoadInst>(DefInst))
      if (auto *UseLoad = dyn_cast_or_null<LoadInst>(UseInst))
        return {!areLoadsReorderable(UseLoad, DefLoad), MayAlias};

  ModRefInfo I = AA.getModRefInfo(DefInst, UseLoc);
  AR = isMustSet(I) ? MustAlias : MayAlias;
  return {isModSet(I) || (treat_loads_as_defs && isRefSet(I)), AR};
}

template <typename AliasAnalysisType>
static ClobberAlias instructionClobbersQuery(MemoryDef *MD,
                                             const MemoryUseOrDef *MU,
                                             const MemoryLocOrCall &UseMLOC,
                                             AliasAnalysisType &AA) {
  // FIXME: This is a temporary hack to allow a single instructionClobbersQuery
  // to exist while MemoryLocOrCall is pushed through places.
  if (UseMLOC.IsCall)
    return instructionClobbersQuery(MD, MemoryLocation(), MU->getMemoryInst(),
                                    AA);
  return instructionClobbersQuery(MD, UseMLOC.getLoc(), MU->getMemoryInst(),
                                  AA);
}

namespace {

struct UpwardsMemoryQuery {
  // True if our original query started off as a call
  bool IsCall = false;
  // The pointer location we started the query with. This will be empty if
  // IsCall is true.
  MemoryLocation StartingLoc;
  // This is the instruction we were querying about.
  const Instruction *Inst = nullptr;
  // The MemoryAccess we actually got called with, used to test local domination
  const MemoryAccess *OriginalAccess = nullptr;
  Optional<AliasResult> AR = MayAlias;
  bool SkipSelfAccess = false;

  UpwardsMemoryQuery() = default;

  UpwardsMemoryQuery(const Instruction *Inst, const MemoryAccess *Access)
      : IsCall(isa<CallBase>(Inst)), Inst(Inst), OriginalAccess(Access) {
    if (!IsCall)
      StartingLoc = MemoryLocation::get(Inst);
  }
};

} // end anonymous namespace

template <typename AliasAnalysisType>
static bool
isUseTriviallyOptimizableToLiveOnEntry(AliasAnalysisType &AA,
                                       const Instruction *I,
                                       bool treat_loads_as_defs = true) {
  if (treat_loads_as_defs)
    return false;
  // If the memory can't be changed, then loads of the memory can't be
  // clobbered.
  if (auto *LI = dyn_cast<LoadInst>(I))
    return I->hasMetadata(LLVMContext::MD_invariant_load) ||
           AA.pointsToConstantMemory(MemoryLocation::get(LI));
  return false;
}

namespace {

/// Our algorithm for walking (and trying to optimize) clobbers, all wrapped up
/// in one class.
template <class AliasAnalysisType> class ClobberWalker {
  /// Save a few bytes by using unsigned instead of size_t.
  using ListIndex = unsigned;

  /// Represents a span of contiguous MemoryDefs, potentially ending in a
  /// MemoryPhi.
  struct DefPath {
    MemoryLocation Loc;
    // Note that, because we always walk in reverse, Last will always dominate
    // First. Also note that First and Last are inclusive.
    MemoryAccess *First;
    MemoryAccess *Last;
    Optional<ListIndex> Previous;

    DefPath(const MemoryLocation &Loc, MemoryAccess *First, MemoryAccess *Last,
            Optional<ListIndex> Previous)
        : Loc(Loc), First(First), Last(Last), Previous(Previous) {}

    DefPath(const MemoryLocation &Loc, MemoryAccess *Init,
            Optional<ListIndex> Previous)
        : DefPath(Loc, Init, Init, Previous) {}
  };

  const FalseMemorySSA &MSSA;
  AliasAnalysisType &AA;
  DominatorTree &DT;
  UpwardsMemoryQuery *Query;
  unsigned *UpwardWalkLimit;

  // Phi optimization bookkeeping:
  // List of DefPath to process during the current phi optimization walk.
  SmallVector<DefPath, 32> Paths;
  // List of visited <Access, Location> pairs; we can skip paths already
  // visited with the same memory location.
  DenseSet<ConstMemoryAccessPair> VisitedPhis;
  // Record if phi translation has been performed during the current phi
  // optimization walk, as merging alias results after phi translation can
  // yield incorrect results. Context in PR46156.
  bool PerformedPhiTranslation = false;

  /// Find the nearest def or phi that `From` can legally be optimized to.
  const MemoryAccess *getWalkTarget(const MemoryPhi *From) const {
    assert(From->getNumOperands() && "Phi with no operands?");

    BasicBlock *BB = From->getBlock();
    MemoryAccess *Result = MSSA.getLiveOnEntryDef();
    DomTreeNode *Node = DT.getNode(BB);
    while ((Node = Node->getIDom())) {
      auto *Defs = MSSA.getBlockDefs(Node->getBlock());
      if (Defs)
        return &*Defs->rbegin();
    }
    return Result;
  }

  /// Result of calling walkToPhiOrClobber.
  struct UpwardsWalkResult {
    /// The "Result" of the walk. Either a clobber, the last thing we walked, or
    /// both. Include alias info when clobber found.
    MemoryAccess *Result;
    bool IsKnownClobber;
    Optional<AliasResult> AR;
  };

  /// Walk to the next Phi or Clobber in the def chain starting at Desc.Last.
  /// This will update Desc.Last as it walks. It will (optionally) also stop at
  /// StopAt.
  ///
  /// This does not test for whether StopAt is a clobber
  UpwardsWalkResult
  walkToPhiOrClobber(DefPath &Desc, const MemoryAccess *StopAt = nullptr,
                     const MemoryAccess *SkipStopAt = nullptr) const {
    assert(!isa<MemoryUse>(Desc.Last) && "Uses don't exist in my world");
    assert(UpwardWalkLimit && "Need a valid walk limit");
    bool LimitAlreadyReached = false;
    // (*UpwardWalkLimit) may be 0 here, due to the loop in tryOptimizePhi. Set
    // it to 1. This will not do any alias() calls. It either returns in the
    // first iteration in the loop below, or is set back to 0 if all def chains
    // are free of MemoryDefs.
    if (!*UpwardWalkLimit) {
      *UpwardWalkLimit = 1;
      LimitAlreadyReached = true;
    }

    for (MemoryAccess *Current : def_chain(Desc.Last)) {
      Desc.Last = Current;
      if (Current == StopAt || Current == SkipStopAt)
        return {Current, false, MayAlias};

      if (auto *MD = dyn_cast<MemoryDef>(Current)) {
        if (MSSA.isLiveOnEntryDef(MD))
          return {MD, true, MustAlias};

        if (!--*UpwardWalkLimit)
          return {Current, true, MayAlias};

        ClobberAlias CA =
            instructionClobbersQuery(MD, Desc.Loc, Query->Inst, AA);
        if (CA.IsClobber)
          return {MD, true, CA.AR};
      }
    }

    if (LimitAlreadyReached)
      *UpwardWalkLimit = 0;

    assert(isa<MemoryPhi>(Desc.Last) &&
           "Ended at a non-clobber that's not a phi?");
    return {Desc.Last, false, MayAlias};
  }

  void addSearches(MemoryPhi *Phi, SmallVectorImpl<ListIndex> &PausedSearches,
                   ListIndex PriorNode) {
#if LLVM_VERSION_MAJOR >= 12
    auto UpwardDefsBegin = upward_defs_begin({Phi, Paths[PriorNode].Loc}, DT,
                                             &PerformedPhiTranslation);
#elif LLVM_VERSION_MAJOR >= 11
    auto UpwardDefsBegin = upward_defs_begin({Phi, Paths[PriorNode].Loc}, DT);
#else
    auto UpwardDefsBegin = upward_defs_begin({Phi, Paths[PriorNode].Loc});
#endif
    auto UpwardDefs = make_range(UpwardDefsBegin, upward_defs_end());
    for (const MemoryAccessPair &P : UpwardDefs) {
      PausedSearches.push_back(Paths.size());
      Paths.emplace_back(P.second, P.first, PriorNode);
    }
  }

  /// Represents a search that terminated after finding a clobber. This clobber
  /// may or may not be present in the path of defs from LastNode..SearchStart,
  /// since it may have been retrieved from cache.
  struct TerminatedPath {
    MemoryAccess *Clobber;
    ListIndex LastNode;
  };

  /// Get an access that keeps us from optimizing to the given phi.
  ///
  /// PausedSearches is an array of indices into the Paths array. Its incoming
  /// value is the indices of searches that stopped at the last phi optimization
  /// target. It's left in an unspecified state.
  ///
  /// If this returns None, NewPaused is a vector of searches that terminated
  /// at StopWhere. Otherwise, NewPaused is left in an unspecified state.
  Optional<TerminatedPath>
  getBlockingAccess(const MemoryAccess *StopWhere,
                    SmallVectorImpl<ListIndex> &PausedSearches,
                    SmallVectorImpl<ListIndex> &NewPaused,
                    SmallVectorImpl<TerminatedPath> &Terminated) {
    assert(!PausedSearches.empty() && "No searches to continue?");

    // BFS vs DFS really doesn't make a difference here, so just do a DFS with
    // PausedSearches as our stack.
    while (!PausedSearches.empty()) {
      ListIndex PathIndex = PausedSearches.pop_back_val();
      DefPath &Node = Paths[PathIndex];

      // If we've already visited this path with this MemoryLocation, we don't
      // need to do so again.
      //
      // NOTE: That we just drop these paths on the ground makes caching
      // behavior sporadic. e.g. given a diamond:
      //  A
      // B C
      //  D
      //
      // ...If we walk D, B, A, C, we'll only cache the result of phi
      // optimization for A, B, and D; C will be skipped because it dies here.
      // This arguably isn't the worst thing ever, since:
      //   - We generally query things in a top-down order, so if we got below D
      //     without needing cache entries for {C, MemLoc}, then chances are
      //     that those cache entries would end up ultimately unused.
      //   - We still cache things for A, so C only needs to walk up a bit.
      // If this behavior becomes problematic, we can fix without a ton of extra
      // work.
      if (!VisitedPhis.insert({Node.Last, Node.Loc}).second) {
        if (PerformedPhiTranslation) {
          // If visiting this path performed Phi translation, don't continue,
          // since it may not be correct to merge results from two paths if one
          // relies on the phi translation.
          TerminatedPath Term{Node.Last, PathIndex};
          return Term;
        }
        continue;
      }

      const MemoryAccess *SkipStopWhere = nullptr;
      if (Query->SkipSelfAccess && Node.Loc == Query->StartingLoc) {
        assert(isa<MemoryDef>(Query->OriginalAccess));
        SkipStopWhere = Query->OriginalAccess;
      }

      UpwardsWalkResult Res = walkToPhiOrClobber(Node,
                                                 /*StopAt=*/StopWhere,
                                                 /*SkipStopAt=*/SkipStopWhere);
      if (Res.IsKnownClobber) {
        assert(Res.Result != StopWhere && Res.Result != SkipStopWhere);

        // If this wasn't a cache hit, we hit a clobber when walking. That's a
        // failure.
        TerminatedPath Term{Res.Result, PathIndex};
        if (!MSSA.dominates(Res.Result, StopWhere))
          return Term;

        // Otherwise, it's a valid thing to potentially optimize to.
        Terminated.push_back(Term);
        continue;
      }

      if (Res.Result == StopWhere || Res.Result == SkipStopWhere) {
        // We've hit our target. Save this path off for if we want to continue
        // walking. If we are in the mode of skipping the OriginalAccess, and
        // we've reached back to the OriginalAccess, do not save path, we've
        // just looped back to self.
        if (Res.Result != SkipStopWhere)
          NewPaused.push_back(PathIndex);
        continue;
      }

      assert(!MSSA.isLiveOnEntryDef(Res.Result) && "liveOnEntry is a clobber");
      addSearches(cast<MemoryPhi>(Res.Result), PausedSearches, PathIndex);
    }

    return None;
  }

  template <typename T, typename Walker>
  struct generic_def_path_iterator
      : public iterator_facade_base<generic_def_path_iterator<T, Walker>,
                                    std::forward_iterator_tag, T *> {
    generic_def_path_iterator() {}
    generic_def_path_iterator(Walker *W, ListIndex N) : W(W), N(N) {}

    T &operator*() const { return curNode(); }

    generic_def_path_iterator &operator++() {
      N = curNode().Previous;
      return *this;
    }

    bool operator==(const generic_def_path_iterator &O) const {
      if (N.hasValue() != O.N.hasValue())
        return false;
      return !N.hasValue() || *N == *O.N;
    }

  private:
    T &curNode() const { return W->Paths[*N]; }

    Walker *W = nullptr;
    Optional<ListIndex> N = None;
  };

  using def_path_iterator = generic_def_path_iterator<DefPath, ClobberWalker>;
  using const_def_path_iterator =
      generic_def_path_iterator<const DefPath, const ClobberWalker>;

  iterator_range<def_path_iterator> def_path(ListIndex From) {
    return make_range(def_path_iterator(this, From), def_path_iterator());
  }

  iterator_range<const_def_path_iterator> const_def_path(ListIndex From) const {
    return make_range(const_def_path_iterator(this, From),
                      const_def_path_iterator());
  }

  struct OptznResult {
    /// The path that contains our result.
    TerminatedPath PrimaryClobber;
    /// The paths that we can legally cache back from, but that aren't
    /// necessarily the result of the Phi optimization.
    SmallVector<TerminatedPath, 4> OtherClobbers;
  };

  ListIndex defPathIndex(const DefPath &N) const {
    // The assert looks nicer if we don't need to do &N
    const DefPath *NP = &N;
    assert(!Paths.empty() && NP >= &Paths.front() && NP <= &Paths.back() &&
           "Out of bounds DefPath!");
    return NP - &Paths.front();
  }

  /// Try to optimize a phi as best as we can. Returns a SmallVector of Paths
  /// that act as legal clobbers. Note that this won't return *all* clobbers.
  ///
  /// Phi optimization algorithm tl;dr:
  ///   - Find the earliest def/phi, A, we can optimize to
  ///   - Find if all paths from the starting memory access ultimately reach A
  ///     - If not, optimization isn't possible.
  ///     - Otherwise, walk from A to another clobber or phi, A'.
  ///       - If A' is a def, we're done.
  ///       - If A' is a phi, try to optimize it.
  ///
  /// A path is a series of {MemoryAccess, MemoryLocation} pairs. A path
  /// terminates when a MemoryAccess that clobbers said MemoryLocation is found.
  OptznResult tryOptimizePhi(MemoryPhi *Phi, MemoryAccess *Start,
                             const MemoryLocation &Loc) {
    assert(Paths.empty() && VisitedPhis.empty() && !PerformedPhiTranslation &&
           "Reset the optimization state.");

    Paths.emplace_back(Loc, Start, Phi, None);
    // Stores how many "valid" optimization nodes we had prior to calling
    // addSearches/getBlockingAccess. Necessary for caching if we had a blocker.
    auto PriorPathsSize = Paths.size();

    SmallVector<ListIndex, 16> PausedSearches;
    SmallVector<ListIndex, 8> NewPaused;
    SmallVector<TerminatedPath, 4> TerminatedPaths;

    addSearches(Phi, PausedSearches, 0);

    // Moves the TerminatedPath with the "most dominated" Clobber to the end of
    // Paths.
    auto MoveDominatedPathToEnd = [&](SmallVectorImpl<TerminatedPath> &Paths) {
      assert(!Paths.empty() && "Need a path to move");
      auto Dom = Paths.begin();
      for (auto I = std::next(Dom), E = Paths.end(); I != E; ++I)
        if (!MSSA.dominates(I->Clobber, Dom->Clobber))
          Dom = I;
      auto Last = Paths.end() - 1;
      if (Last != Dom)
        std::iter_swap(Last, Dom);
    };

    MemoryPhi *Current = Phi;
    while (true) {
      assert(!MSSA.isLiveOnEntryDef(Current) &&
             "liveOnEntry wasn't treated as a clobber?");

      const auto *Target = getWalkTarget(Current);
      // If a TerminatedPath doesn't dominate Target, then it wasn't a legal
      // optimization for the prior phi.
      assert(all_of(TerminatedPaths, [&](const TerminatedPath &P) {
        return MSSA.dominates(P.Clobber, Target);
      }));

      // FIXME: This is broken, because the Blocker may be reported to be
      // liveOnEntry, and we'll happily wait for that to disappear (read: never)
      // For the moment, this is fine, since we do nothing with blocker info.
      if (Optional<TerminatedPath> Blocker = getBlockingAccess(
              Target, PausedSearches, NewPaused, TerminatedPaths)) {

        // Find the node we started at. We can't search based on N->Last, since
        // we may have gone around a loop with a different MemoryLocation.
        auto Iter = find_if(def_path(Blocker->LastNode), [&](const DefPath &N) {
          return defPathIndex(N) < PriorPathsSize;
        });
        assert(Iter != def_path_iterator());

        DefPath &CurNode = *Iter;
        assert(CurNode.Last == Current);

        // Two things:
        // A. We can't reliably cache all of NewPaused back. Consider a case
        //    where we have two paths in NewPaused; one of which can't optimize
        //    above this phi, whereas the other can. If we cache the second path
        //    back, we'll end up with suboptimal cache entries. We can handle
        //    cases like this a bit better when we either try to find all
        //    clobbers that block phi optimization, or when our cache starts
        //    supporting unfinished searches.
        // B. We can't reliably cache TerminatedPaths back here without doing
        //    extra checks; consider a case like:
        //       T
        //      / \
        //     D   C
        //      \ /
        //       S
        //    Where T is our target, C is a node with a clobber on it, D is a
        //    diamond (with a clobber *only* on the left or right node, N), and
        //    S is our start. Say we walk to D, through the node opposite N
        //    (read: ignoring the clobber), and see a cache entry in the top
        //    node of D. That cache entry gets put into TerminatedPaths. We then
        //    walk up to C (N is later in our worklist), find the clobber, and
        //    quit. If we append TerminatedPaths to OtherClobbers, we'll cache
        //    the bottom part of D to the cached clobber, ignoring the clobber
        //    in N. Again, this problem goes away if we start tracking all
        //    blockers for a given phi optimization.
        TerminatedPath Result{CurNode.Last, defPathIndex(CurNode)};
        return {Result, {}};
      }

      // If there's nothing left to search, then all paths led to valid clobbers
      // that we got from our cache; pick the nearest to the start, and allow
      // the rest to be cached back.
      if (NewPaused.empty()) {
        MoveDominatedPathToEnd(TerminatedPaths);
        TerminatedPath Result = TerminatedPaths.pop_back_val();
        return {Result, std::move(TerminatedPaths)};
      }

      MemoryAccess *DefChainEnd = nullptr;
      SmallVector<TerminatedPath, 4> Clobbers;
      for (ListIndex Paused : NewPaused) {
        UpwardsWalkResult WR = walkToPhiOrClobber(Paths[Paused]);
        if (WR.IsKnownClobber)
          Clobbers.push_back({WR.Result, Paused});
        else
          // Micro-opt: If we hit the end of the chain, save it.
          DefChainEnd = WR.Result;
      }

      if (!TerminatedPaths.empty()) {
        // If we couldn't find the dominating phi/liveOnEntry in the above loop,
        // do it now.
        if (!DefChainEnd)
          for (auto *MA : def_chain(const_cast<MemoryAccess *>(Target)))
            DefChainEnd = MA;
        assert(DefChainEnd && "Failed to find dominating phi/liveOnEntry");

        // If any of the terminated paths don't dominate the phi we'll try to
        // optimize, we need to figure out what they are and quit.
        const BasicBlock *ChainBB = DefChainEnd->getBlock();
        for (const TerminatedPath &TP : TerminatedPaths) {
          // Because we know that DefChainEnd is as "high" as we can go, we
          // don't need local dominance checks; BB dominance is sufficient.
          if (DT.dominates(ChainBB, TP.Clobber->getBlock()))
            Clobbers.push_back(TP);
        }
      }

      // If we have clobbers in the def chain, find the one closest to Current
      // and quit.
      if (!Clobbers.empty()) {
        MoveDominatedPathToEnd(Clobbers);
        TerminatedPath Result = Clobbers.pop_back_val();
        return {Result, std::move(Clobbers)};
      }

      assert(all_of(NewPaused,
                    [&](ListIndex I) { return Paths[I].Last == DefChainEnd; }));

      // Because liveOnEntry is a clobber, this must be a phi.
      auto *DefChainPhi = cast<MemoryPhi>(DefChainEnd);

      PriorPathsSize = Paths.size();
      PausedSearches.clear();
      for (ListIndex I : NewPaused)
        addSearches(DefChainPhi, PausedSearches, I);
      NewPaused.clear();

      Current = DefChainPhi;
    }
  }

  void verifyOptResult(const OptznResult &R) const {
    assert(all_of(R.OtherClobbers, [&](const TerminatedPath &P) {
      return MSSA.dominates(P.Clobber, R.PrimaryClobber.Clobber);
    }));
  }

  void resetPhiOptznState() {
    Paths.clear();
    VisitedPhis.clear();
    PerformedPhiTranslation = false;
  }

public:
  ClobberWalker(const FalseMemorySSA &MSSA, AliasAnalysisType &AA,
                DominatorTree &DT)
      : MSSA(MSSA), AA(AA), DT(DT) {}

  AliasAnalysisType *getAA() { return &AA; }
  /// Finds the nearest clobber for the given query, optimizing phis if
  /// possible.
  MemoryAccess *findClobber(MemoryAccess *Start, UpwardsMemoryQuery &Q,
                            unsigned &UpWalkLimit) {
    Query = &Q;
    UpwardWalkLimit = &UpWalkLimit;
    // Starting limit must be > 0.
    if (!UpWalkLimit)
      UpWalkLimit++;

    MemoryAccess *Current = Start;
    // This walker pretends uses don't exist. If we're handed one, silently grab
    // its def. (This has the nice side-effect of ensuring we never cache uses)
    if (auto *MU = dyn_cast<MemoryUse>(Start))
      Current = MU->getDefiningAccess();

    DefPath FirstDesc(Q.StartingLoc, Current, Current, None);
    // Fast path for the overly-common case (no crazy phi optimization
    // necessary)
    UpwardsWalkResult WalkResult = walkToPhiOrClobber(FirstDesc);
    MemoryAccess *Result;
    if (WalkResult.IsKnownClobber) {
      Result = WalkResult.Result;
      Q.AR = WalkResult.AR;
    } else {
      OptznResult OptRes = tryOptimizePhi(cast<MemoryPhi>(FirstDesc.Last),
                                          Current, Q.StartingLoc);
      verifyOptResult(OptRes);
      resetPhiOptznState();
      Result = OptRes.PrimaryClobber.Clobber;
    }

    return Result;
  }
};

struct RenamePassData {
  DomTreeNode *DTN;
  DomTreeNode::const_iterator ChildIt;
  MemoryAccess *IncomingVal;

  RenamePassData(DomTreeNode *D, DomTreeNode::const_iterator It,
                 MemoryAccess *M)
      : DTN(D), ChildIt(It), IncomingVal(M) {}

  void swap(RenamePassData &RHS) {
    std::swap(DTN, RHS.DTN);
    std::swap(ChildIt, RHS.ChildIt);
    std::swap(IncomingVal, RHS.IncomingVal);
  }
};

} // end anonymous namespace

namespace llvm {

template <class AliasAnalysisType> class FalseMemorySSA::ClobberWalkerBase {
  ClobberWalker<AliasAnalysisType> Walker;
  FalseMemorySSA *MSSA;

public:
  ClobberWalkerBase(FalseMemorySSA *M, AliasAnalysisType *A, DominatorTree *D)
      : Walker(*M, *A, *D), MSSA(M) {}

  MemoryAccess *getClobberingMemoryAccessBase(MemoryAccess *,
                                              const MemoryLocation &,
                                              unsigned &);
  // Third argument (bool), defines whether the clobber search should skip the
  // original queried access. If true, there will be a follow-up query searching
  // for a clobber access past "self". Note that the Optimized access is not
  // updated if a new clobber is found by this SkipSelf search. If this
  // additional query becomes heavily used we may decide to cache the result.
  // Walker instantiations will decide how to set the SkipSelf bool.
  MemoryAccess *getClobberingMemoryAccessBase(MemoryAccess *, unsigned &, bool);
};

/// A FalseMemorySSAWalker that does AA walks to disambiguate accesses. It no
/// longer does caching on its own, but the name has been retained for the
/// moment.
template <class AliasAnalysisType>
class FalseMemorySSA::CachingWalker final : public FalseMemorySSAWalker {
  ClobberWalkerBase<AliasAnalysisType> *Walker;

public:
  CachingWalker(FalseMemorySSA *M, ClobberWalkerBase<AliasAnalysisType> *W)
      : FalseMemorySSAWalker(M), Walker(W) {}
  ~CachingWalker() override = default;

  using FalseMemorySSAWalker::getClobberingMemoryAccess;

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA, unsigned &UWL) {
    return Walker->getClobberingMemoryAccessBase(MA, UWL, false);
  }
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc,
                                          unsigned &UWL) {
    return Walker->getClobberingMemoryAccessBase(MA, Loc, UWL);
  }

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA) override {
    unsigned UpwardWalkLimit = MaxCheckLimit;
    return getClobberingMemoryAccess(MA, UpwardWalkLimit);
  }
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc) override {
    unsigned UpwardWalkLimit = MaxCheckLimit;
    return getClobberingMemoryAccess(MA, Loc, UpwardWalkLimit);
  }

  void invalidateInfo(MemoryAccess *MA) override {
    if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
      MUD->resetOptimized();
  }
};

} // end namespace llvm

void FalseMemorySSA::renameSuccessorPhis(BasicBlock *BB,
                                         MemoryAccess *IncomingVal,
                                         bool RenameAllUses) {
  // Pass through values to our successors
  for (const BasicBlock *S : successors(BB)) {
    auto It = PerBlockAccesses.find(S);
    // Rename the phi nodes in our successor block
    if (It == PerBlockAccesses.end() || !isa<MemoryPhi>(It->second->front()))
      continue;
    AccessList *Accesses = It->second.get();
    auto *Phi = cast<MemoryPhi>(&Accesses->front());
    if (RenameAllUses) {
      bool ReplacementDone = false;
      for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I)
        if (Phi->getIncomingBlock(I) == BB) {
          Phi->setIncomingValue(I, IncomingVal);
          ReplacementDone = true;
        }
      (void)ReplacementDone;
      assert(ReplacementDone && "Incomplete phi during partial rename");
    } else {
      Phi->addIncoming(IncomingVal, BB);
    }
  }
}

/// Rename a single basic block into FalseMemorySSA form.
/// Uses the standard SSA renaming algorithm.
/// \returns The new incoming value.
MemoryAccess *FalseMemorySSA::renameBlock(BasicBlock *BB,
                                          MemoryAccess *IncomingVal,
                                          bool RenameAllUses) {
  auto It = PerBlockAccesses.find(BB);
  // Skip most processing if the list is empty.
  if (It != PerBlockAccesses.end()) {
    AccessList *Accesses = It->second.get();
    for (MemoryAccess &L : *Accesses) {
      if (MemoryUseOrDef *MUD = dyn_cast<MemoryUseOrDef>(&L)) {
        if (MUD->getDefiningAccess() == nullptr || RenameAllUses)
          MUD->setDefiningAccess(IncomingVal);
        if (isa<MemoryDef>(&L))
          IncomingVal = &L;
      } else {
        IncomingVal = &L;
      }
    }
  }
  return IncomingVal;
}

/// This is the standard SSA renaming algorithm.
///
/// We walk the dominator tree in preorder, renaming accesses, and then filling
/// in phi nodes in our successors.
void FalseMemorySSA::renamePass(DomTreeNode *Root, MemoryAccess *IncomingVal,
                                SmallPtrSetImpl<BasicBlock *> &Visited,
                                bool SkipVisited, bool RenameAllUses) {
  assert(Root && "Trying to rename accesses in an unreachable block");

  SmallVector<RenamePassData, 32> WorkStack;
  // Skip everything if we already renamed this block and we are skipping.
  // Note: You can't sink this into the if, because we need it to occur
  // regardless of whether we skip blocks or not.
  bool AlreadyVisited = !Visited.insert(Root->getBlock()).second;
  if (SkipVisited && AlreadyVisited)
    return;

  IncomingVal = renameBlock(Root->getBlock(), IncomingVal, RenameAllUses);
  renameSuccessorPhis(Root->getBlock(), IncomingVal, RenameAllUses);
  WorkStack.push_back({Root, Root->begin(), IncomingVal});

  while (!WorkStack.empty()) {
    DomTreeNode *Node = WorkStack.back().DTN;
    DomTreeNode::const_iterator ChildIt = WorkStack.back().ChildIt;
    IncomingVal = WorkStack.back().IncomingVal;

    if (ChildIt == Node->end()) {
      WorkStack.pop_back();
    } else {
      DomTreeNode *Child = *ChildIt;
      ++WorkStack.back().ChildIt;
      BasicBlock *BB = Child->getBlock();
      // Note: You can't sink this into the if, because we need it to occur
      // regardless of whether we skip blocks or not.
      AlreadyVisited = !Visited.insert(BB).second;
      if (SkipVisited && AlreadyVisited) {
        // We already visited this during our renaming, which can happen when
        // being asked to rename multiple blocks. Figure out the incoming val,
        // which is the last def.
        // Incoming value can only change if there is a block def, and in that
        // case, it's the last block def in the list.
        if (auto *BlockDefs = getWritableBlockDefs(BB))
          IncomingVal = &*BlockDefs->rbegin();
      } else {
        IncomingVal = renameBlock(BB, IncomingVal, RenameAllUses);
      }
      renameSuccessorPhis(BB, IncomingVal, RenameAllUses);
      WorkStack.push_back({Child, Child->begin(), IncomingVal});
    }
  }
}

/// This handles unreachable block accesses by deleting phi nodes in
/// unreachable blocks, and marking all other unreachable MemoryAccess's as
/// being uses of the live on entry definition.
void FalseMemorySSA::markUnreachableAsLiveOnEntry(BasicBlock *BB) {
  assert(!DT->isReachableFromEntry(BB) &&
         "Reachable block found while handling unreachable blocks");

  // Make sure phi nodes in our reachable successors end up with a
  // LiveOnEntryDef for our incoming edge, even though our block is forward
  // unreachable.  We could just disconnect these blocks from the CFG fully,
  // but we do not right now.
  for (const BasicBlock *S : successors(BB)) {
    if (!DT->isReachableFromEntry(S))
      continue;
    auto It = PerBlockAccesses.find(S);
    // Rename the phi nodes in our successor block
    if (It == PerBlockAccesses.end() || !isa<MemoryPhi>(It->second->front()))
      continue;
    AccessList *Accesses = It->second.get();
    auto *Phi = cast<MemoryPhi>(&Accesses->front());
    Phi->addIncoming(LiveOnEntryDef.get(), BB);
  }

  auto It = PerBlockAccesses.find(BB);
  if (It == PerBlockAccesses.end())
    return;

  auto &Accesses = It->second;
  for (auto AI = Accesses->begin(), AE = Accesses->end(); AI != AE;) {
    auto Next = std::next(AI);
    // If we have a phi, just remove it. We are going to replace all
    // users with live on entry.
    if (auto *UseOrDef = dyn_cast<MemoryUseOrDef>(AI))
      UseOrDef->setDefiningAccess(LiveOnEntryDef.get());
    else
      Accesses->erase(AI);
    AI = Next;
  }
}

FalseMemorySSA::FalseMemorySSA(Function &Func, AliasAnalysis *AA,
                               DominatorTree *DT)
    : AA(nullptr), DT(DT), F(Func), LiveOnEntryDef(nullptr), Walker(nullptr),
      NextID(0) {
  // Build FalseMemorySSA using a batch alias analysis. This reuses the internal
  // state that AA collects during an alias()/getModRefInfo() call. This is
  // safe because there are no CFG changes while building FalseMemorySSA and can
  // significantly reduce the time spent by the compiler in AA, because we will
  // make queries about all the instructions in the Function.
  assert(AA && "No alias analysis?");
  BatchAAResults BatchAA(*AA);
  buildMemorySSA(BatchAA);
  // Intentionally leave AA to nullptr while building so we don't accidently
  // use non-batch AliasAnalysis.
  this->AA = AA;
  // Also create the walker here.
  getWalker();
}

FalseMemorySSA::~FalseMemorySSA() {
  // Drop all our references
  for (const auto &Pair : PerBlockAccesses)
    for (MemoryAccess &MA : *Pair.second)
      MA.dropAllReferences();
}

FalseMemorySSA::AccessList *
FalseMemorySSA::getOrCreateAccessList(const BasicBlock *BB) {
  auto Res = PerBlockAccesses.insert(std::make_pair(BB, nullptr));

  if (Res.second)
    Res.first->second = std::make_unique<AccessList>();
  return Res.first->second.get();
}

FalseMemorySSA::DefsList *
FalseMemorySSA::getOrCreateDefsList(const BasicBlock *BB) {
  auto Res = PerBlockDefs.insert(std::make_pair(BB, nullptr));

  if (Res.second)
    Res.first->second = std::make_unique<DefsList>();
  return Res.first->second.get();
}

namespace llvm {

/// This class is a batch walker of all MemoryUse's in the program, and points
/// their defining access at the thing that actually clobbers them.  Because it
/// is a batch walker that touches everything, it does not operate like the
/// other walkers.  This walker is basically performing a top-down SSA renaming
/// pass, where the version stack is used as the cache.  This enables it to be
/// significantly more time and memory efficient than using the regular walker,
/// which is walking bottom-up.
class FalseMemorySSA::OptimizeUses {
public:
  OptimizeUses(FalseMemorySSA *MSSA, CachingWalker<BatchAAResults> *Walker,
               BatchAAResults *BAA, DominatorTree *DT)
      : MSSA(MSSA), Walker(Walker), AA(BAA), DT(DT) {}

  void optimizeUses();

private:
  /// This represents where a given memorylocation is in the stack.
  struct MemlocStackInfo {
    // This essentially is keeping track of versions of the stack. Whenever
    // the stack changes due to pushes or pops, these versions increase.
    size_t StackEpoch;
    size_t PopEpoch;
    // This is the lower bound of places on the stack to check. It is equal to
    // the place the last stack walk ended.
    // Note: Correctness depends on this being initialized to 0, which densemap
    // does
    size_t LowerBound;
    const BasicBlock *LowerBoundBlock;
    // This is where the last walk for this memory location ended.
    size_t LastKill;
    bool LastKillValid;
    Optional<AliasResult> AR;
  };

  void optimizeUsesInBlock(const BasicBlock *, size_t &, size_t &,
                           SmallVectorImpl<MemoryAccess *> &,
                           DenseMap<MemoryLocOrCall, MemlocStackInfo> &);

  FalseMemorySSA *MSSA;
  CachingWalker<BatchAAResults> *Walker;
  BatchAAResults *AA;
  DominatorTree *DT;
};

} // end namespace llvm

/// Optimize the uses in a given block This is basically the SSA renaming
/// algorithm, with one caveat: We are able to use a single stack for all
/// MemoryUses.  This is because the set of *possible* reaching MemoryDefs is
/// the same for every MemoryUse.  The *actual* clobbering MemoryDef is just
/// going to be some position in that stack of possible ones.
///
/// We track the stack positions that each MemoryLocation needs
/// to check, and last ended at.  This is because we only want to check the
/// things that changed since last time.  The same MemoryLocation should
/// get clobbered by the same store (getModRefInfo does not use invariantness or
/// things like this, and if they start, we can modify MemoryLocOrCall to
/// include relevant data)
void FalseMemorySSA::OptimizeUses::optimizeUsesInBlock(
    const BasicBlock *BB, size_t &StackEpoch, size_t &PopEpoch,
    SmallVectorImpl<MemoryAccess *> &VersionStack,
    DenseMap<MemoryLocOrCall, MemlocStackInfo> &LocStackInfo) {

  /// If no accesses, nothing to do.
  FalseMemorySSA::AccessList *Accesses = MSSA->getWritableBlockAccesses(BB);
  if (Accesses == nullptr)
    return;

  // Pop everything that doesn't dominate the current block off the stack,
  // increment the PopEpoch to account for this.
  while (true) {
    assert(
        !VersionStack.empty() &&
        "Version stack should have liveOnEntry sentinel dominating everything");
    BasicBlock *BackBlock = VersionStack.back()->getBlock();
    if (DT->dominates(BackBlock, BB))
      break;
    while (VersionStack.back()->getBlock() == BackBlock)
      VersionStack.pop_back();
    ++PopEpoch;
  }

  for (MemoryAccess &MA : *Accesses) {
    auto *MU = dyn_cast<MemoryUse>(&MA);
    if (!MU) {
      VersionStack.push_back(&MA);
      ++StackEpoch;
      continue;
    }

    if (isUseTriviallyOptimizableToLiveOnEntry(*AA, MU->getMemoryInst())) {
      MU->setDefiningAccess(MSSA->getLiveOnEntryDef(), true, None);
      continue;
    }

    MemoryLocOrCall UseMLOC(MU);
    auto &LocInfo = LocStackInfo[UseMLOC];
    // If the pop epoch changed, it means we've removed stuff from top of
    // stack due to changing blocks. We may have to reset the lower bound or
    // last kill info.
    if (LocInfo.PopEpoch != PopEpoch) {
      LocInfo.PopEpoch = PopEpoch;
      LocInfo.StackEpoch = StackEpoch;
      // If the lower bound was in something that no longer dominates us, we
      // have to reset it.
      // We can't simply track stack size, because the stack may have had
      // pushes/pops in the meantime.
      // XXX: This is non-optimal, but only is slower cases with heavily
      // branching dominator trees.  To get the optimal number of queries would
      // be to make lowerbound and lastkill a per-loc stack, and pop it until
      // the top of that stack dominates us.  This does not seem worth it ATM.
      // A much cheaper optimization would be to always explore the deepest
      // branch of the dominator tree first. This will guarantee this resets on
      // the smallest set of blocks.
      if (LocInfo.LowerBoundBlock && LocInfo.LowerBoundBlock != BB &&
          !DT->dominates(LocInfo.LowerBoundBlock, BB)) {
        // Reset the lower bound of things to check.
        // TODO: Some day we should be able to reset to last kill, rather than
        // 0.
        LocInfo.LowerBound = 0;
        LocInfo.LowerBoundBlock = VersionStack[0]->getBlock();
        LocInfo.LastKillValid = false;
      }
    } else if (LocInfo.StackEpoch != StackEpoch) {
      // If all that has changed is the StackEpoch, we only have to check the
      // new things on the stack, because we've checked everything before.  In
      // this case, the lower bound of things to check remains the same.
      LocInfo.PopEpoch = PopEpoch;
      LocInfo.StackEpoch = StackEpoch;
    }
    if (!LocInfo.LastKillValid) {
      LocInfo.LastKill = VersionStack.size() - 1;
      LocInfo.LastKillValid = true;
      LocInfo.AR = MayAlias;
    }

    // At this point, we should have corrected last kill and LowerBound to be
    // in bounds.
    assert(LocInfo.LowerBound < VersionStack.size() &&
           "Lower bound out of range");
    assert(LocInfo.LastKill < VersionStack.size() &&
           "Last kill info out of range");
    // In any case, the new upper bound is the top of the stack.
    size_t UpperBound = VersionStack.size() - 1;

    if (UpperBound - LocInfo.LowerBound > MaxCheckLimit) {
      LLVM_DEBUG(dbgs() << "FalseMemorySSA skipping optimization of " << *MU
                        << " (" << *(MU->getMemoryInst()) << ")"
                        << " because there are "
                        << UpperBound - LocInfo.LowerBound
                        << " stores to disambiguate\n");
      // Because we did not walk, LastKill is no longer valid, as this may
      // have been a kill.
      LocInfo.LastKillValid = false;
      continue;
    }
    bool FoundClobberResult = false;
    unsigned UpwardWalkLimit = MaxCheckLimit;
    while (UpperBound > LocInfo.LowerBound) {
      if (isa<MemoryPhi>(VersionStack[UpperBound])) {
        // For phis, use the walker, see where we ended up, go there
        MemoryAccess *Result =
            Walker->getClobberingMemoryAccess(MU, UpwardWalkLimit);
        // We are guaranteed to find it or something is wrong
        while (VersionStack[UpperBound] != Result) {
          assert(UpperBound != 0);
          --UpperBound;
        }
        FoundClobberResult = true;
        break;
      }

      MemoryDef *MD = cast<MemoryDef>(VersionStack[UpperBound]);
      ClobberAlias CA = instructionClobbersQuery(MD, MU, UseMLOC, *AA);
      if (CA.IsClobber) {
        FoundClobberResult = true;
        LocInfo.AR = CA.AR;
        break;
      }
      --UpperBound;
    }

    // Note: Phis always have AliasResult AR set to MayAlias ATM.

    // At the end of this loop, UpperBound is either a clobber, or lower bound
    // PHI walking may cause it to be < LowerBound, and in fact, < LastKill.
    if (FoundClobberResult || UpperBound < LocInfo.LastKill) {
      // We were last killed now by where we got to
      if (MSSA->isLiveOnEntryDef(VersionStack[UpperBound]))
        LocInfo.AR = None;
      MU->setDefiningAccess(VersionStack[UpperBound], true, LocInfo.AR);
      LocInfo.LastKill = UpperBound;
    } else {
      // Otherwise, we checked all the new ones, and now we know we can get to
      // LastKill.
      MU->setDefiningAccess(VersionStack[LocInfo.LastKill], true, LocInfo.AR);
    }
    LocInfo.LowerBound = VersionStack.size() - 1;
    LocInfo.LowerBoundBlock = BB;
  }
}

/// Optimize uses to point to their actual clobbering definitions.
void FalseMemorySSA::OptimizeUses::optimizeUses() {
  SmallVector<MemoryAccess *, 16> VersionStack;
  DenseMap<MemoryLocOrCall, MemlocStackInfo> LocStackInfo;
  VersionStack.push_back(MSSA->getLiveOnEntryDef());

  size_t StackEpoch = 1;
  size_t PopEpoch = 1;
  // We perform a non-recursive top-down dominator tree walk.
  for (const auto *DomNode : depth_first(DT->getRootNode()))
    optimizeUsesInBlock(DomNode->getBlock(), StackEpoch, PopEpoch, VersionStack,
                        LocStackInfo);
}

void FalseMemorySSA::placePHINodes(
    const SmallPtrSetImpl<BasicBlock *> &DefiningBlocks) {
  // Determine where our MemoryPhi's should go
  ForwardIDFCalculator IDFs(*DT);
  IDFs.setDefiningBlocks(DefiningBlocks);
  SmallVector<BasicBlock *, 32> IDFBlocks;
  IDFs.calculate(IDFBlocks);

  // Now place MemoryPhi nodes.
  for (auto &BB : IDFBlocks)
    createMemoryPhi(BB);
}

void FalseMemorySSA::buildMemorySSA(BatchAAResults &BAA) {
  // We create an access to represent "live on entry", for things like
  // arguments or users of globals, where the memory they use is defined before
  // the beginning of the function. We do not actually insert it into the IR.
  // We do not define a live on exit for the immediate uses, and thus our
  // semantics do *not* imply that something with no immediate uses can simply
  // be removed.
  BasicBlock &StartingPoint = F.getEntryBlock();
  LiveOnEntryDef.reset(new MemoryDef(F.getContext(), nullptr, nullptr,
                                     &StartingPoint, NextID++));

  // We maintain lists of memory accesses per-block, trading memory for time. We
  // could just look up the memory access for every possible instruction in the
  // stream.
  SmallPtrSet<BasicBlock *, 32> DefiningBlocks;
  // Go through each block, figure out where defs occur, and chain together all
  // the accesses.
  for (BasicBlock &B : F) {
    bool InsertIntoDef = false;
    AccessList *Accesses = nullptr;
    DefsList *Defs = nullptr;
    for (Instruction &I : B) {
      MemoryUseOrDef *MUD = createNewAccess(&I, &BAA);
      if (!MUD)
        continue;

      if (!Accesses)
        Accesses = getOrCreateAccessList(&B);
      Accesses->push_back(MUD);
      if (isa<MemoryDef>(MUD)) {
        InsertIntoDef = true;
        if (!Defs)
          Defs = getOrCreateDefsList(&B);
        Defs->push_back(*MUD);
      }
    }
    if (InsertIntoDef)
      DefiningBlocks.insert(&B);
  }
  placePHINodes(DefiningBlocks);

  // Now do regular SSA renaming on the MemoryDef/MemoryUse. Visited will get
  // filled in with all blocks.
  SmallPtrSet<BasicBlock *, 16> Visited;
  renamePass(DT->getRootNode(), LiveOnEntryDef.get(), Visited);

  ClobberWalkerBase<BatchAAResults> WalkerBase(this, &BAA, DT);
  CachingWalker<BatchAAResults> WalkerLocal(this, &WalkerBase);
  OptimizeUses(this, &WalkerLocal, &BAA, DT).optimizeUses();

  // Mark the uses in unreachable blocks as live on entry, so that they go
  // somewhere.
  for (auto &BB : F)
    if (!Visited.count(&BB))
      markUnreachableAsLiveOnEntry(&BB);
}

FalseMemorySSAWalker *FalseMemorySSA::getWalker() { return getWalkerImpl(); }

FalseMemorySSA::CachingWalker<AliasAnalysis> *FalseMemorySSA::getWalkerImpl() {
  if (Walker)
    return Walker.get();

  if (!WalkerBase)
    WalkerBase =
        std::make_unique<ClobberWalkerBase<AliasAnalysis>>(this, AA, DT);

  Walker =
      std::make_unique<CachingWalker<AliasAnalysis>>(this, WalkerBase.get());
  return Walker.get();
}

// This is a helper function used by the creation routines. It places NewAccess
// into the access and defs lists for a given basic block, at the given
// insertion point.
void FalseMemorySSA::insertIntoListsForBlock(MemoryAccess *NewAccess,
                                             const BasicBlock *BB,
                                             InsertionPlace Point) {
  auto *Accesses = getOrCreateAccessList(BB);
  if (Point == Beginning) {
    // If it's a phi node, it goes first, otherwise, it goes after any phi
    // nodes.
    if (isa<MemoryPhi>(NewAccess)) {
      Accesses->push_front(NewAccess);
      auto *Defs = getOrCreateDefsList(BB);
      Defs->push_front(*NewAccess);
    } else {
      auto AI = find_if_not(
          *Accesses, [](const MemoryAccess &MA) { return isa<MemoryPhi>(MA); });
      Accesses->insert(AI, NewAccess);
      if (!isa<MemoryUse>(NewAccess)) {
        auto *Defs = getOrCreateDefsList(BB);
        auto DI = find_if_not(
            *Defs, [](const MemoryAccess &MA) { return isa<MemoryPhi>(MA); });
        Defs->insert(DI, *NewAccess);
      }
    }
  } else {
    Accesses->push_back(NewAccess);
    if (!isa<MemoryUse>(NewAccess)) {
      auto *Defs = getOrCreateDefsList(BB);
      Defs->push_back(*NewAccess);
    }
  }
  BlockNumberingValid.erase(BB);
}

void FalseMemorySSA::insertIntoListsBefore(MemoryAccess *What,
                                           const BasicBlock *BB,
                                           AccessList::iterator InsertPt) {
  auto *Accesses = getWritableBlockAccesses(BB);
  bool WasEnd = InsertPt == Accesses->end();
  Accesses->insert(AccessList::iterator(InsertPt), What);
  if (!isa<MemoryUse>(What)) {
    auto *Defs = getOrCreateDefsList(BB);
    // If we got asked to insert at the end, we have an easy job, just shove it
    // at the end. If we got asked to insert before an existing def, we also get
    // an iterator. If we got asked to insert before a use, we have to hunt for
    // the next def.
    if (WasEnd) {
      Defs->push_back(*What);
    } else if (isa<MemoryDef>(InsertPt)) {
      Defs->insert(InsertPt->getDefsIterator(), *What);
    } else {
      while (InsertPt != Accesses->end() && !isa<MemoryDef>(InsertPt))
        ++InsertPt;
      // Either we found a def, or we are inserting at the end
      if (InsertPt == Accesses->end())
        Defs->push_back(*What);
      else
        Defs->insert(InsertPt->getDefsIterator(), *What);
    }
  }
  BlockNumberingValid.erase(BB);
}

MemoryPhi *FalseMemorySSA::createMemoryPhi(BasicBlock *BB) {
  assert(!getMemoryAccess(BB) && "MemoryPhi already exists for this BB");
  MemoryPhi *Phi = new MemoryPhi(BB->getContext(), BB, NextID++);
  // Phi's always are placed at the front of the block.
  insertIntoListsForBlock(Phi, BB, Beginning);
  ValueToMemoryAccess[BB] = Phi;
  return Phi;
}

// Return true if the instruction has ordering constraints.
// Note specifically that this only considers stores and loads
// because others are still considered ModRef by getModRefInfo.
static inline bool isOrdered(const Instruction *I) {
  if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (!SI->isUnordered())
      return true;
  } else if (auto *LI = dyn_cast<LoadInst>(I)) {
    if (!LI->isUnordered())
      return true;
  }
  return false;
}

/// Helper function to create new memory accesses
template <typename AliasAnalysisType>
MemoryUseOrDef *
FalseMemorySSA::createNewAccess(Instruction *I, AliasAnalysisType *AAP,
                                const MemoryUseOrDef *Template) {
  const bool treat_loads_as_defs = true;

  // The assume intrinsic has a control dependency which we model by claiming
  // that it writes arbitrarily. Debuginfo intrinsics may be considered
  // clobbers when we have a nonstandard AA pipeline. Ignore these fake memory
  // dependencies here.
  // FIXME: Replace this special casing with a more accurate modelling of
  // assume's control dependency.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::assume:
#if LLVM_VERSION_MAJOR >= 12
    case Intrinsic::experimental_noalias_scope_decl:
#endif
      return nullptr;
    }
  }

  // Using a nonstandard AA pipelines might leave us with unexpected modref
  // results for I, so add a check to not model instructions that may not read
  // from or write to memory. This is necessary for correctness.
  if (!I->mayReadFromMemory() && !I->mayWriteToMemory())
    return nullptr;

  bool Def, Use;
  if (Template) {
    Def = isa<MemoryDef>(Template);
    Use = isa<MemoryUse>(Template);
  } else {
    // Find out what affect this instruction has on memory.
    ModRefInfo ModRef = AAP->getModRefInfo(I, None);
    // The isOrdered check is used to ensure that volatiles end up as defs
    // (atomics end up as ModRef right now anyway).  Until we separate the
    // ordering chain from the memory chain, this enables people to see at least
    // some relative ordering to volatiles.  Note that getClobberingMemoryAccess
    // will still give an answer that bypasses other volatile loads.  TODO:
    // Separate memory aliasing and ordering into two different chains so that
    // we can precisely represent both "what memory will this read/write/is
    // clobbered by" and "what instructions can I move this past".
    Def = isModSet(ModRef) || isOrdered(I) ||
          (treat_loads_as_defs && isRefSet(ModRef));
    Use = isRefSet(ModRef);
  }

  // It's possible for an instruction to not modify memory at all. During
  // construction, we ignore them.
  if (!Def && !Use)
    return nullptr;

  MemoryUseOrDef *MUD;
  if (Def)
    MUD = new MemoryDef(I->getContext(), nullptr, I, I->getParent(), NextID++);
  else
    MUD = new MemoryUse(I->getContext(), nullptr, I, I->getParent());
  ValueToMemoryAccess[I] = MUD;
  return MUD;
}

void FalseMemorySSA::print(raw_ostream &OS) const {
  FalseMemorySSAAnnotatedWriter Writer(this);
  F.print(OS, &Writer);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void FalseMemorySSA::dump() const { print(dbgs()); }
#endif

/// Perform a local numbering on blocks so that instruction ordering can be
/// determined in constant time.
/// TODO: We currently just number in order.  If we numbered by N, we could
/// allow at least N-1 sequences of insertBefore or insertAfter (and at least
/// log2(N) sequences of mixed before and after) without needing to invalidate
/// the numbering.
void FalseMemorySSA::renumberBlock(const BasicBlock *B) const {
  // The pre-increment ensures the numbers really start at 1.
  size_t CurrentNumber = 0;
  const AccessList *AL = getBlockAccesses(B);
  assert(AL != nullptr && "Asking to renumber an empty block");
  for (const auto &I : *AL)
    BlockNumbering[&I] = ++CurrentNumber;
  BlockNumberingValid.insert(B);
}

/// Determine, for two memory accesses in the same block,
/// whether \p Dominator dominates \p Dominatee.
/// \returns True if \p Dominator dominates \p Dominatee.
bool FalseMemorySSA::locallyDominates(const MemoryAccess *Dominator,
                                      const MemoryAccess *Dominatee) const {
  const BasicBlock *DominatorBlock = Dominator->getBlock();

  assert((DominatorBlock == Dominatee->getBlock()) &&
         "Asking for local domination when accesses are in different blocks!");
  // A node dominates itself.
  if (Dominatee == Dominator)
    return true;

  // When Dominatee is defined on function entry, it is not dominated by another
  // memory access.
  if (isLiveOnEntryDef(Dominatee))
    return false;

  // When Dominator is defined on function entry, it dominates the other memory
  // access.
  if (isLiveOnEntryDef(Dominator))
    return true;

  if (!BlockNumberingValid.count(DominatorBlock))
    renumberBlock(DominatorBlock);

  size_t DominatorNum = BlockNumbering.lookup(Dominator);
  // All numbers start with 1
  assert(DominatorNum != 0 && "Block was not numbered properly");
  size_t DominateeNum = BlockNumbering.lookup(Dominatee);
  assert(DominateeNum != 0 && "Block was not numbered properly");
  return DominatorNum < DominateeNum;
}

bool FalseMemorySSA::dominates(const MemoryAccess *Dominator,
                               const MemoryAccess *Dominatee) const {
  if (Dominator == Dominatee)
    return true;

  if (isLiveOnEntryDef(Dominatee))
    return false;

  if (Dominator->getBlock() != Dominatee->getBlock())
    return DT->dominates(Dominator->getBlock(), Dominatee->getBlock());
  return locallyDominates(Dominator, Dominatee);
}

bool FalseMemorySSA::dominates(const MemoryAccess *Dominator,
                               const Use &Dominatee) const {
  if (MemoryPhi *MP = dyn_cast<MemoryPhi>(Dominatee.getUser())) {
    BasicBlock *UseBB = MP->getIncomingBlock(Dominatee);
    // The def must dominate the incoming block of the phi.
    if (UseBB != Dominator->getBlock())
      return DT->dominates(Dominator->getBlock(), UseBB);
    // If the UseBB and the DefBB are the same, compare locally.
    return locallyDominates(Dominator, cast<MemoryAccess>(Dominatee));
  }
  // If it's not a PHI node use, the normal dominates can already handle it.
  return dominates(Dominator, cast<MemoryAccess>(Dominatee.getUser()));
}

AnalysisKey FalseMemorySSAAnalysis::Key;

FalseMemorySSAAnalysis::Result
FalseMemorySSAAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  return FalseMemorySSAAnalysis::Result(
      std::make_unique<FalseMemorySSA>(F, &AA, &DT));
}

bool FalseMemorySSAAnalysis::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  auto PAC = PA.getChecker<FalseMemorySSAAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>()) ||
         Inv.invalidate<AAManager>(F, PA) ||
         Inv.invalidate<DominatorTreeAnalysis>(F, PA);
}

PreservedAnalyses FalseMemorySSAPrinterPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  auto &MSSA = AM.getResult<FalseMemorySSAAnalysis>(F).getMSSA();
  OS << "FalseMemorySSA for function: " << F.getName() << "\n";
  MSSA.print(OS);

  return PreservedAnalyses::all();
}

char FalseMemorySSAWrapperPass::ID = 0;

FalseMemorySSAWrapperPass::FalseMemorySSAWrapperPass() : FunctionPass(ID) {}

void FalseMemorySSAWrapperPass::releaseMemory() { MSSA.reset(); }

void FalseMemorySSAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<AAResultsWrapperPass>();
}

bool FalseMemorySSAWrapperPass::runOnFunction(Function &F) {
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  MSSA.reset(new FalseMemorySSA(F, &AA, &DT));
  return false;
}

void FalseMemorySSAWrapperPass::verifyAnalysis() const {}

void FalseMemorySSAWrapperPass::print(raw_ostream &OS, const Module *M) const {
  MSSA->print(OS);
}

FalseMemorySSAWalker::FalseMemorySSAWalker(FalseMemorySSA *M) : MSSA(M) {}

/// Walk the use-def chains starting at \p StartingAccess and find
/// the MemoryAccess that actually clobbers Loc.
///
/// \returns our clobbering memory access
template <typename AliasAnalysisType>
MemoryAccess *FalseMemorySSA::ClobberWalkerBase<AliasAnalysisType>::
    getClobberingMemoryAccessBase(MemoryAccess *StartingAccess,
                                  const MemoryLocation &Loc,
                                  unsigned &UpwardWalkLimit) {
  if (isa<MemoryPhi>(StartingAccess))
    return StartingAccess;

  auto *StartingUseOrDef = cast<MemoryUseOrDef>(StartingAccess);
  if (MSSA->isLiveOnEntryDef(StartingUseOrDef))
    return StartingUseOrDef;

  Instruction *I = StartingUseOrDef->getMemoryInst();

  // Conservatively, fences are always clobbers, so don't perform the walk if we
  // hit a fence.
  if (!isa<CallBase>(I) && I->isFenceLike())
    return StartingUseOrDef;

  UpwardsMemoryQuery Q;
  Q.OriginalAccess = StartingUseOrDef;
  Q.StartingLoc = Loc;
  Q.Inst = nullptr;
  Q.IsCall = false;

  // Unlike the other function, do not walk to the def of a def, because we are
  // handed something we already believe is the clobbering access.
  // We never set SkipSelf to true in Q in this method.
  MemoryAccess *DefiningAccess = isa<MemoryUse>(StartingUseOrDef)
                                     ? StartingUseOrDef->getDefiningAccess()
                                     : StartingUseOrDef;

  MemoryAccess *Clobber =
      Walker.findClobber(DefiningAccess, Q, UpwardWalkLimit);
  LLVM_DEBUG(dbgs() << "Starting Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *StartingUseOrDef << "\n");
  LLVM_DEBUG(dbgs() << "Final Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *Clobber << "\n");
  return Clobber;
}

template <typename AliasAnalysisType>
MemoryAccess *FalseMemorySSA::ClobberWalkerBase<
    AliasAnalysisType>::getClobberingMemoryAccessBase(MemoryAccess *MA,
                                                      unsigned &UpwardWalkLimit,
                                                      bool SkipSelf) {
  auto *StartingAccess = dyn_cast<MemoryUseOrDef>(MA);
  // If this is a MemoryPhi, we can't do anything.
  if (!StartingAccess)
    return MA;

  bool IsOptimized = false;

  // If this is an already optimized use or def, return the optimized result.
  // Note: Currently, we store the optimized def result in a separate field,
  // since we can't use the defining access.
  if (StartingAccess->isOptimized()) {
    if (!SkipSelf || !isa<MemoryDef>(StartingAccess))
      return StartingAccess->getOptimized();
    IsOptimized = true;
  }

  const Instruction *I = StartingAccess->getMemoryInst();
  // We can't sanely do anything with a fence, since they conservatively clobber
  // all memory, and have no locations to get pointers from to try to
  // disambiguate.
  if (!isa<CallBase>(I) && I->isFenceLike())
    return StartingAccess;

  UpwardsMemoryQuery Q(I, StartingAccess);

  if (isUseTriviallyOptimizableToLiveOnEntry(*Walker.getAA(), I)) {
    MemoryAccess *LiveOnEntry = MSSA->getLiveOnEntryDef();
    StartingAccess->setOptimized(LiveOnEntry);
    StartingAccess->setOptimizedAccessType(None);
    return LiveOnEntry;
  }

  MemoryAccess *OptimizedAccess;
  if (!IsOptimized) {
    // Start with the thing we already think clobbers this location
    MemoryAccess *DefiningAccess = StartingAccess->getDefiningAccess();

    // At this point, DefiningAccess may be the live on entry def.
    // If it is, we will not get a better result.
    if (MSSA->isLiveOnEntryDef(DefiningAccess)) {
      StartingAccess->setOptimized(DefiningAccess);
      StartingAccess->setOptimizedAccessType(None);
      return DefiningAccess;
    }

    OptimizedAccess = Walker.findClobber(DefiningAccess, Q, UpwardWalkLimit);
    StartingAccess->setOptimized(OptimizedAccess);
    if (MSSA->isLiveOnEntryDef(OptimizedAccess))
      StartingAccess->setOptimizedAccessType(None);
    else if (Q.AR == MustAlias)
      StartingAccess->setOptimizedAccessType(MustAlias);
  } else {
    OptimizedAccess = StartingAccess->getOptimized();
  }

  LLVM_DEBUG(dbgs() << "Starting Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *StartingAccess << "\n");
  LLVM_DEBUG(dbgs() << "Optimized Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *OptimizedAccess << "\n");

  MemoryAccess *Result;
  if (SkipSelf && isa<MemoryPhi>(OptimizedAccess) &&
      isa<MemoryDef>(StartingAccess) && UpwardWalkLimit) {
    assert(isa<MemoryDef>(Q.OriginalAccess));
    Q.SkipSelfAccess = true;
    Result = Walker.findClobber(OptimizedAccess, Q, UpwardWalkLimit);
  } else {
    Result = OptimizedAccess;
  }

  LLVM_DEBUG(dbgs() << "Result Memory SSA clobber [SkipSelf = " << SkipSelf);
  LLVM_DEBUG(dbgs() << "] for " << *I << " is " << *Result << "\n");

  return Result;
}

namespace {
struct RegisterPassX : RegisterPass<FalseMemorySSAWrapperPass> {
  RegisterPassX()
      : RegisterPass("false-memoryssa", "False Memory SSA", false, true) {
    // Ensure required passes are loaded, even if this pass used in a program
    // that doesn't load all the standard LLVM passes.
    initializeDominatorTreeWrapperPassPass(*PassRegistry::getPassRegistry());
    initializeAAResultsWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};
} // end anonymous namespace
static RegisterPassX X;
