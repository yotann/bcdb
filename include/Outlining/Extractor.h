#ifndef BCDB_OUTLINING_EXTRACTOR_H
#define BCDB_OUTLINING_EXTRACTOR_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/SparseBitVector.h>
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
class Type;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

class OutliningExtractor {
public:
  OutliningExtractor(Function &F, OutliningDependenceResults &OutDep,
                     SparseBitVector<> &BV);

  Function *createNewCallee();
  Function *createNewCaller();
  unsigned getNumCalleeArgs() const;
  unsigned getNumCalleeReturnValues() const;
  void getArgTypes(SmallVectorImpl<Type *> &types) const;
  void getResultTypes(SmallVectorImpl<Type *> &types) const;

  Function &F;
  OutliningDependenceResults &OutDep;
  SparseBitVector<> &BV;

private:
  FunctionType *CalleeType = nullptr;
  Function *NewCallee = nullptr;
  Function *NewCaller = nullptr;
  SparseBitVector<> OutlinedBlocks;
  SparseBitVector<> ArgInputs, ExternalInputs, ExternalOutputs;

  // PHI nodes that were chosen for outlining, but which depend on control flow
  // outside the outlined set.
  SparseBitVector<> input_phis;

  // PHI nodes that were not chosen for outlining, but which depend on control
  // flow in the outlined set.
  SparseBitVector<> output_phis;

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
  DenseMap<Function *, std::vector<std::pair<SparseBitVector<>, Function *>>>
      NewFunctions;
};

} // end namespace bcdb

#endif // BCDB_OUTLINING_EXTRACTOR_H
