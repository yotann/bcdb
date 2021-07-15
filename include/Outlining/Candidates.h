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

class OutliningCandidates {
public:
  OutliningCandidates(Function &F, OutliningDependenceResults &OutDep);

  void print(raw_ostream &OS) const;

  Function &F;
  OutliningDependenceResults &OutDep;

  std::vector<SparseBitVector<>> Candidates;

private:
  void generateCandidatesEndingAt(size_t i);
  void emitCandidate(const SparseBitVector<> &bv);
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
