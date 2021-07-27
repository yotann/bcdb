#ifndef BCDB_OUTLINING_EXTRACTOR_H
#define BCDB_OUTLINING_EXTRACTOR_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <llvm/Transforms/Utils/ValueMapper.h>
#include <string>
#include <utility>
#include <vector>

#include "Candidates.h"
#include "Dependence.h"

namespace llvm {
class AnalysisUsage;
class BasicBlock;
class Function;
class IntegerType;
class Module;
class Type;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

class OutliningCalleeExtractor {
public:
  OutliningCalleeExtractor(Function &function,
                           const OutliningDependenceResults &deps,
                           const SparseBitVector<> &bv);

  Function *createDeclaration();
  Function *createDefinition();
  unsigned getNumArgs() const;
  unsigned getNumReturnValues() const;
  void getArgTypes(SmallVectorImpl<Type *> &types) const;
  void getResultTypes(SmallVectorImpl<Type *> &types) const;

  Function &function;
  const OutliningDependenceResults &deps;
  const SparseBitVector<> &bv;

private:
  friend class OutliningCallerExtractor;

  Function *new_callee = nullptr;
  SparseBitVector<> outlined_blocks;
  SparseBitVector<> arg_inputs, external_inputs, external_outputs;

  // PHI nodes that were chosen for outlining, but which depend on control flow
  // outside the outlined set.
  SparseBitVector<> input_phis;

  // PHI nodes that were not chosen for outlining, but which depend on control
  // flow in the outlined set.
  SparseBitVector<> output_phis;
};

class OutliningCallerExtractor {
public:
  OutliningCallerExtractor(Function &function,
                           const OutliningDependenceResults &deps,
                           const std::vector<SparseBitVector<>> &bvs);

  void modifyDefinition();

  Function &function;
  const OutliningDependenceResults &deps;
  const std::vector<SparseBitVector<>> &bvs;
  std::vector<OutliningCalleeExtractor> callees;

private:
  // Find a successor of bb that leads to its immediate postdominator, or
  // nullptr if there is no such successor.
  BasicBlock *findNextBlock(BasicBlock *bb);

  void handleSingleCallee(const SparseBitVector<> &bv,
                          OutliningCalleeExtractor &callee);

  DenseMap<Value *, Value *> replacements;
  SmallVector<Instruction *, 16> insns_to_delete;
  BasicBlock *unreachable_block;
  IntegerType *i32_type;
  bool already_modified = false;
};

// Needs to be a module pass because it adds new functions.
class OutliningExtractorPass : public PassInfoMixin<OutliningExtractorPass> {
public:
  OutliningExtractorPass() {}

  PreservedAnalyses run(Module &m, ModuleAnalysisManager &am);
};

// Needs to be a ModulePass because it adds new functions.
struct OutliningExtractorWrapperPass : public ModulePass {
  OutliningExtractorWrapperPass();

  static char ID;

  bool runOnModule(Module &M) override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // end namespace bcdb

#endif // BCDB_OUTLINING_EXTRACTOR_H
