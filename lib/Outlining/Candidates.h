#ifndef BCDB_OUTLINING_CANDIDATES_H
#define BCDB_OUTLINING_CANDIDATES_H

#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/Optional.h>
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

struct BitVectorCompare {
  bool operator()(const BitVector &A, const BitVector &B) const {
    BitVector C = A;
    C ^= B;
    if (!C.any())
      return false;
    return B.test(C.find_first());
  }
};

class OutliningCandidates {
public:
  OutliningCandidates(Function &F, OutliningDependenceResults &OutDep);

  void print(raw_ostream &OS) const;

  Function &F;
  OutliningDependenceResults &OutDep;

  std::vector<BitVector> Candidates;

private:
  std::vector<BitVector> Queue;
  std::set<BitVector, BitVectorCompare> AlreadyVisited;

  void createInitialCandidates();
  void queueBV(BitVector BV);
  void processCandidate(BitVector BV);
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
