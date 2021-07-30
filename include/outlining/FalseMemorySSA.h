//
// The FalseMemorySSA analysis is copied from LLVM 12's MemorySSA, with these
// changes:
//
// - It has been changed to treat all memory accesses as if they were stores.
//   This means it can be used to identify write-after-read dependences
//   (antidependences), where the original MemorySSA focuses on
//   read-after-write dependences ("true" dependences).
// - It has been changed to build successfully with multiple versions of LLVM.
// - Some unneeded code has been removed.
// - TODO: Can we also make it more precise? Can we apply OptimizeUses to defs
//   too, or would that break things? Would be useful for both write-after-read
//   and write-after-write dependences.
//

#ifndef BCDB_FALSEMEMORYSSA_H
#define BCDB_FALSEMEMORYSSA_H

#include "friendly_MemorySSA.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedUser.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>

namespace llvm {

class AllocaInst;
class Function;
class Instruction;
class FalseMemorySSAWalker;
class LLVMContext;
class raw_ostream;

/// Encapsulates FalseMemorySSA, including all data associated with memory
/// accesses.
class FalseMemorySSA {
public:
  FalseMemorySSA(Function &, AliasAnalysis *, DominatorTree *);

  // MemorySSA must remain where it's constructed; Walkers it creates store
  // pointers to it.
  FalseMemorySSA(FalseMemorySSA &&) = delete;

  ~FalseMemorySSA();

  FalseMemorySSAWalker *getWalker();

  /// Given a memory Mod/Ref'ing instruction, get the MemorySSA
  /// access associated with it. If passed a basic block gets the memory phi
  /// node that exists for that block, if there is one. Otherwise, this will get
  /// a MemoryUseOrDef.
  MemoryUseOrDef *getMemoryAccess(const Instruction *I) const {
    return cast_or_null<MemoryUseOrDef>(ValueToMemoryAccess.lookup(I));
  }

  MemoryPhi *getMemoryAccess(const BasicBlock *BB) const {
    return cast_or_null<MemoryPhi>(ValueToMemoryAccess.lookup(cast<Value>(BB)));
  }

  DominatorTree &getDomTree() const { return *DT; }

  void dump() const;
  void print(raw_ostream &) const;

  /// Return true if \p MA represents the live on entry value
  ///
  /// Loads and stores from pointer arguments and other global values may be
  /// defined by memory operations that do not occur in the current function, so
  /// they may be live on entry to the function. MemorySSA represents such
  /// memory state by the live on entry definition, which is guaranteed to occur
  /// before any other memory access in the function.
  inline bool isLiveOnEntryDef(const MemoryAccess *MA) const {
    return MA == LiveOnEntryDef.get();
  }

  inline MemoryAccess *getLiveOnEntryDef() const {
    return LiveOnEntryDef.get();
  }

  // Sadly, iplists, by default, owns and deletes pointers added to the
  // list. It's not currently possible to have two iplists for the same type,
  // where one owns the pointers, and one does not. This is because the traits
  // are per-type, not per-tag.  If this ever changes, we should make the
  // DefList an iplist.
  using AccessList = iplist<MemoryAccess, ilist_tag<MSSAHelpers::AllAccessTag>>;
  using DefsList =
      simple_ilist<MemoryAccess, ilist_tag<MSSAHelpers::DefsOnlyTag>>;

  /// Return the list of MemoryAccess's for a given basic block.
  ///
  /// This list is not modifiable by the user.
  const AccessList *getBlockAccesses(const BasicBlock *BB) const {
    return getWritableBlockAccesses(BB);
  }

  /// Return the list of MemoryDef's and MemoryPhi's for a given basic
  /// block.
  ///
  /// This list is not modifiable by the user.
  const DefsList *getBlockDefs(const BasicBlock *BB) const {
    return getWritableBlockDefs(BB);
  }

  /// Given two memory accesses in the same basic block, determine
  /// whether MemoryAccess \p A dominates MemoryAccess \p B.
  bool locallyDominates(const MemoryAccess *A, const MemoryAccess *B) const;

  /// Given two memory accesses in potentially different blocks,
  /// determine whether MemoryAccess \p A dominates MemoryAccess \p B.
  bool dominates(const MemoryAccess *A, const MemoryAccess *B) const;

  /// Given a MemoryAccess and a Use, determine whether MemoryAccess \p A
  /// dominates Use \p B.
  bool dominates(const MemoryAccess *A, const Use &B) const;

  /// Verify that MemorySSA is self consistent (IE definitions dominate
  /// all uses, uses appear in the right places).  This is used by unit tests.
  void verifyMemorySSA() const;

  /// Used in various insertion functions to specify whether we are talking
  /// about the beginning or end of a block.
  enum InsertionPlace { Beginning, End, BeforeTerminator };

protected:
  // Used by Memory SSA annotater, dumpers, and wrapper pass
  friend class FalseMemorySSAAnnotatedWriter;

  // This is used by the use optimizer and updater.
  AccessList *getWritableBlockAccesses(const BasicBlock *BB) const {
    auto It = PerBlockAccesses.find(BB);
    return It == PerBlockAccesses.end() ? nullptr : It->second.get();
  }

  // This is used by the use optimizer and updater.
  DefsList *getWritableBlockDefs(const BasicBlock *BB) const {
    auto It = PerBlockDefs.find(BB);
    return It == PerBlockDefs.end() ? nullptr : It->second.get();
  }

  // Rename the dominator tree branch rooted at BB.
  void renamePass(BasicBlock *BB, MemoryAccess *IncomingVal,
                  SmallPtrSetImpl<BasicBlock *> &Visited) {
    renamePass(DT->getNode(BB), IncomingVal, Visited, true, true);
  }

  void insertIntoListsForBlock(MemoryAccess *, const BasicBlock *,
                               InsertionPlace);
  void insertIntoListsBefore(MemoryAccess *, const BasicBlock *,
                             AccessList::iterator);

private:
  template <class AliasAnalysisType> class ClobberWalkerBase;
  template <class AliasAnalysisType> class CachingWalker;
  class OptimizeUses;

  CachingWalker<AliasAnalysis> *getWalkerImpl();
  void buildMemorySSA(BatchAAResults &BAA);

  using AccessMap = DenseMap<const BasicBlock *, std::unique_ptr<AccessList>>;
  using DefsMap = DenseMap<const BasicBlock *, std::unique_ptr<DefsList>>;

  void markUnreachableAsLiveOnEntry(BasicBlock *BB);
  MemoryPhi *createMemoryPhi(BasicBlock *BB);
  template <typename AliasAnalysisType>
  MemoryUseOrDef *createNewAccess(Instruction *, AliasAnalysisType *,
                                  const MemoryUseOrDef *Template = nullptr);
  void placePHINodes(const SmallPtrSetImpl<BasicBlock *> &);
  MemoryAccess *renameBlock(BasicBlock *, MemoryAccess *, bool);
  void renameSuccessorPhis(BasicBlock *, MemoryAccess *, bool);
  void renamePass(DomTreeNode *, MemoryAccess *IncomingVal,
                  SmallPtrSetImpl<BasicBlock *> &Visited,
                  bool SkipVisited = false, bool RenameAllUses = false);
  AccessList *getOrCreateAccessList(const BasicBlock *);
  DefsList *getOrCreateDefsList(const BasicBlock *);
  void renumberBlock(const BasicBlock *) const;
  AliasAnalysis *AA;
  DominatorTree *DT;
  Function &F;

  // Memory SSA mappings
  DenseMap<const Value *, MemoryAccess *> ValueToMemoryAccess;

  // These two mappings contain the main block to access/def mappings for
  // MemorySSA. The list contained in PerBlockAccesses really owns all the
  // MemoryAccesses.
  // Both maps maintain the invariant that if a block is found in them, the
  // corresponding list is not empty, and if a block is not found in them, the
  // corresponding list is empty.
  AccessMap PerBlockAccesses;
  DefsMap PerBlockDefs;
  std::unique_ptr<MemoryAccess, ValueDeleter> LiveOnEntryDef;

  // Domination mappings
  // Note that the numbering is local to a block, even though the map is
  // global.
  mutable SmallPtrSet<const BasicBlock *, 16> BlockNumberingValid;
  mutable DenseMap<const MemoryAccess *, unsigned long> BlockNumbering;

  // Memory SSA building info
  std::unique_ptr<ClobberWalkerBase<AliasAnalysis>> WalkerBase;
  std::unique_ptr<CachingWalker<AliasAnalysis>> Walker;
  unsigned NextID;
};

/// An analysis that produces \c FalseMemorySSA for a function.
///
class FalseMemorySSAAnalysis
    : public AnalysisInfoMixin<FalseMemorySSAAnalysis> {
  friend AnalysisInfoMixin<FalseMemorySSAAnalysis>;

  static AnalysisKey Key;

public:
  // Wrap FalseMemorySSA result to ensure address stability of internal
  // FalseMemorySSA pointers after construction.  Use a wrapper class instead
  // of plain unique_ptr<FalseMemorySSA> to avoid build breakage on MSVC.
  struct Result {
    Result(std::unique_ptr<FalseMemorySSA> &&MSSA) : MSSA(std::move(MSSA)) {}

    FalseMemorySSA &getMSSA() { return *MSSA.get(); }

    std::unique_ptr<FalseMemorySSA> MSSA;

    bool invalidate(Function &F, const PreservedAnalyses &PA,
                    FunctionAnalysisManager::Invalidator &Inv);
  };

  Result run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for \c FalseMemorySSA.
class FalseMemorySSAPrinterPass
    : public PassInfoMixin<FalseMemorySSAPrinterPass> {
  raw_ostream &OS;

public:
  explicit FalseMemorySSAPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy analysis pass which computes \c FalseMemorySSA.
class FalseMemorySSAWrapperPass : public FunctionPass {
public:
  FalseMemorySSAWrapperPass();

  static char ID;

  bool runOnFunction(Function &) override;
  void releaseMemory() override;
  FalseMemorySSA &getMSSA() { return *MSSA; }
  const FalseMemorySSA &getMSSA() const { return *MSSA; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void verifyAnalysis() const override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;

private:
  std::unique_ptr<FalseMemorySSA> MSSA;
};

/// This is the generic walker interface for walkers of FalseMemorySSA.
/// Walkers are used to be able to further disambiguate the def-use chains
/// FalseMemorySSA gives you, or otherwise produce better info than
/// FalseMemorySSA gives / you.
/// In particular, while the def-use chains provide basic information, and are
/// guaranteed to give, for example, the nearest may-aliasing MemoryDef for a
/// MemoryUse as AliasAnalysis considers it, a user mant want better or other
/// information. In particular, they may want to use SCEV info to further
/// disambiguate memory accesses, or they may want the nearest dominating
/// may-aliasing MemoryDef for a call or a store. This API enables a
/// standardized interface to getting and using that info.
class FalseMemorySSAWalker {
public:
  FalseMemorySSAWalker(FalseMemorySSA *);
  virtual ~FalseMemorySSAWalker() = default;

  using MemoryAccessSet = SmallVector<MemoryAccess *, 8>;

  /// Given a memory Mod/Ref/ModRef'ing instruction, calling this
  /// will give you the nearest dominating MemoryAccess that Mod's the location
  /// the instruction accesses (by skipping any def which AA can prove does not
  /// alias the location(s) accessed by the instruction given).
  ///
  /// Note that this will return a single access, and it must dominate the
  /// Instruction, so if an operand of a MemoryPhi node Mod's the instruction,
  /// this will return the MemoryPhi, not the operand. This means that
  /// given:
  /// if (a) {
  ///   1 = MemoryDef(liveOnEntry)
  ///   store %a
  /// } else {
  ///   2 = MemoryDef(liveOnEntry)
  ///   store %b
  /// }
  /// 3 = MemoryPhi(2, 1)
  /// MemoryUse(3)
  /// load %a
  ///
  /// calling this API on load(%a) will return the MemoryPhi, not the MemoryDef
  /// in the if (a) branch.
  MemoryAccess *getClobberingMemoryAccess(const Instruction *I) {
    MemoryAccess *MA = MSSA->getMemoryAccess(I);
    assert(MA &&
           "Handed an instruction that FalseMemorySSA doesn't recognize?");
    return getClobberingMemoryAccess(MA);
  }

  /// Does the same thing as getClobberingMemoryAccess(const Instruction *I),
  /// but takes a MemoryAccess instead of an Instruction.
  virtual MemoryAccess *getClobberingMemoryAccess(MemoryAccess *) = 0;

  /// Given a potentially clobbering memory access and a new location,
  /// calling this will give you the nearest dominating clobbering MemoryAccess
  /// (by skipping non-aliasing def links).
  ///
  /// This version of the function is mainly used to disambiguate phi translated
  /// pointers, where the value of a pointer may have changed from the initial
  /// memory access. Note that this expects to be handed either a MemoryUse,
  /// or an already potentially clobbering access. Unlike the above API, if
  /// given a MemoryDef that clobbers the pointer as the starting access, it
  /// will return that MemoryDef, whereas the above would return the clobber
  /// starting from the use side of  the memory def.
  virtual MemoryAccess *getClobberingMemoryAccess(MemoryAccess *,
                                                  const MemoryLocation &) = 0;

  /// Given a memory access, invalidate anything this walker knows about
  /// that access.
  /// This API is used by walkers that store information to perform basic cache
  /// invalidation.  This will be called by FalseMemorySSA at appropriate times
  /// for the walker it uses or returns.
  virtual void invalidateInfo(MemoryAccess *) {}

protected:
  FalseMemorySSA *MSSA;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYSSA_H
