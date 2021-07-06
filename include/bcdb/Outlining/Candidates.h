#ifndef BCDB_OUTLINING_CANDIDATES_H
#define BCDB_OUTLINING_CANDIDATES_H

#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <set>
#include <vector>

#include "Dependence.h"

namespace llvm {
class AnalysisUsage;
class Function;
class Module;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

// TODO: delete this.
struct SparseBitVectorCompare {
  bool operator()(const SparseBitVector<> &a,
                  const SparseBitVector<> &b) const {
    if (a == b)
      return false;
    auto ia = a.begin(), ib = b.begin();
    auto iae = a.end(), ibe = b.end();
    for (; ia != iae && ib != ibe; ++ia, ++ib) {
      if (*ia > *ib)
        return true;
      if (*ia < *ib)
        return false;
    }
    assert(ia != iae || ib != ibe);
    return ib != ibe;
  }
};

class OutliningCandidates {
public:
  OutliningCandidates(Function &F, OutliningDependenceResults &OutDep);

  void print(raw_ostream &OS) const;

  Function &F;
  OutliningDependenceResults &OutDep;

  std::vector<SparseBitVector<>> Candidates;

private:
  std::vector<SparseBitVector<>> Queue;
  std::set<SparseBitVector<>, SparseBitVectorCompare> AlreadyVisited;

  void createInitialCandidates();
  void queueBV(SparseBitVector<> BV);
  void processCandidate(SparseBitVector<> BV);
};

struct OutliningCandidatesWrapperPass : public FunctionPass {
  OutliningCandidatesWrapperPass();

  static char ID;

  bool runOnFunction(Function &F) override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override;
  void verifyAnalysis() const override;
  OutliningCandidates &getOutCands() { return *OutCands; }

private:
  Optional<OutliningCandidates> OutCands;
};

} // end namespace bcdb

#endif // BCDB_OUTLINING_CANDIDATES_H
