#ifndef BCDB_OUTLINING_EXTRACTOR_H
#define BCDB_OUTLINING_EXTRACTOR_H

#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/Pass.h>
#include <string>
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

  Function *createNewCallee();
  Function *createNewCaller();

  Function &F;
  OutliningDependenceResults &OutDep;
  BitVector &BV;

private:
  Function *NewCallee;
  Function *NewCaller;
  bool OutliningReturn = false;
  BitVector OutlinedBlocks;
  BitVector ArgInputs, ExternalInputs, ExternalOutputs;
  std::string NewName;
};

// Needs to be a ModulePass because it adds new functions.
struct OutliningExtractorWrapperPass : public ModulePass {
  OutliningExtractorWrapperPass();

  static char ID;

  bool runOnModule(Module &M) override;
  bool runOnFunction(Function &F);
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  DenseMap<Function *, std::vector<std::pair<BitVector, Function *>>>
      NewFunctions;
};

} // end namespace bcdb

#endif // BCDB_OUTLINING_EXTRACTOR_H
