#ifndef BCDB_OUTLINING_EXTRACTOR_H
#define BCDB_OUTLINING_EXTRACTOR_H

#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/Pass.h>
#include <utility>
#include <vector>

#include "Candidates.h"
#include "Dependence.h"

namespace llvm {
class AnalysisUsage;
class Function;
class Module;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

class OutliningExtractor {
public:
  OutliningExtractor(Function &F, OutliningDependenceResults &OutDep,
                     BitVector &BV);

  void print(raw_ostream &OS) const;

  Function &F;
  OutliningDependenceResults &OutDep;
  BitVector &BV;

  Function *NewF;

private:
};

struct OutliningExtractorWrapperPass : public ModulePass {
  OutliningExtractorWrapperPass();

  static char ID;

  bool runOnModule(Module &M) override;
  std::vector<std::pair<BitVector, Function *>> runOnFunction(Function &F);
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  DenseMap<Function *, std::vector<std::pair<BitVector, Function *>>>
      NewFunctions;
};

} // end namespace bcdb

#endif // BCDB_OUTLINING_EXTRACTOR_H
