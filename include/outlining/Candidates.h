#ifndef BCDB_OUTLINING_CANDIDATES_H
#define BCDB_OUTLINING_CANDIDATES_H

#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <set>
#include <vector>

#include "Dependence.h"
#include "SizeModel.h"

namespace llvm {
class AnalysisUsage;
class Function;
class GlobalValue;
class Module;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

class OutliningCandidates {
public:
  struct Candidate {
    SparseBitVector<> bv;
    int caller_savings = 0;
    int callee_size = 0;
    SmallVector<Type *, 8> arg_types;
    SmallVector<Type *, 8> result_types;
    SmallPtrSet<GlobalValue *, 1> globals_used;
  };

  // size_model may be nullptr to disable profitability checks.
  OutliningCandidates(Function &F, OutliningDependenceResults &OutDep,
                      const SizeModelResults *size_model);

  void print(raw_ostream &OS) const;

  Function &F;
  OutliningDependenceResults &OutDep;
  const SizeModelResults *size_model;

  std::vector<Candidate> Candidates;

private:
  void generateCandidatesEndingAt(size_t i);
  void emitCandidate(Candidate &candidate);
  bool addNode(Candidate &candidate, size_t i);
};

class OutliningCandidatesAnalysis
    : public AnalysisInfoMixin<OutliningCandidatesAnalysis> {
  friend AnalysisInfoMixin<OutliningCandidatesAnalysis>;

  static AnalysisKey Key;

public:
  using Result = OutliningCandidates;

  OutliningCandidates run(Function &f, FunctionAnalysisManager &am);
};

class OutliningCandidatesPrinterPass
    : public PassInfoMixin<OutliningCandidatesPrinterPass> {
  raw_ostream &os;

public:
  explicit OutliningCandidatesPrinterPass(raw_ostream &os) : os(os) {}

  PreservedAnalyses run(Function &f, FunctionAnalysisManager &am);
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
