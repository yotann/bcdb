#ifndef BCDB_OUTLINING_DEPENDENCE_H
#define BCDB_OUTLINING_DEPENDENCE_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <memory>
#include <optional>
#include <vector>

#include "outlining/CorrectPostDominatorTree.h"

namespace llvm {
class AnalysisUsage;
class BasicBlock;
class DominatorTree;
class Function;
class Instruction;
class MemoryPhi;
class MemorySSA;
class Module;
class PostDominatorTree;
class Value;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using namespace llvm;

// Calculates dependence information used to decide which sets of instructions
// can be outlined. The primary results are ForcedDepends and DominatingDepends,
// which have indices specified by Nodes and NodeIndices. DataDepends and
// ArgDepends are also used to determine input and output values for the
// outlined callee.
class OutliningDependenceResults {
public:
  OutliningDependenceResults(Function &F, DominatorTree &DT,
                             PostDominatorTree &PDT, MemorySSA &MSSA);

  OutliningDependenceResults(OutliningDependenceResults &&rhs) = default;

  void print(raw_ostream &OS) const;

  // Check whether a candidate can be legally outlined.
  bool isOutlinable(const SparseBitVector<> &BV) const;

  // Print the bitvector, in the form "1.3.5_10".
  void printSet(llvm::raw_ostream &os, const SparseBitVector<> &bv,
                llvm::StringRef sep = ", ", llvm::StringRef range = "-") const;

  // Fill out the ForcedDepends and DominatingDepends with additional necessary
  // dependences. May be slow.
  void computeTransitiveClosures();

  // Each node must be one of the following types:
  // - Instruction
  // - BasicBlock, used before instructions to represent control dependencies
  // - MemoryPhi, used immediately after BasicBlock
  std::vector<Value *> Nodes;

  // We guarantee Nodes[NodeIndices[V]] == V.
  DenseMap<Value *, size_t> NodeIndices;

  // If PreventsOutlining.test(i) is true, Nodes[i] may never be outlined.
  SparseBitVector<> PreventsOutlining;

  // If CompilesToCall.test(i) is true, Nodes[i] is likely to be compiled into
  // a call instruction.
  SparseBitVector<> CompilesToCall;

  // If DataDepends[i].test(j) is true, Nodes[i] has a data dependency on
  // Nodes[j].
  std::vector<SparseBitVector<>> DataDepends;

  // If ArgDepends[i].test(j) is true, Nodes[i] has a data dependency on
  // function argument j.
  std::vector<SparseBitVector<>> ArgDepends;

  // If Dominators[i].test(j) is true, Nodes[i] is dominated by Nodes[j]. Note
  // that nodes dominate themselves--Dominators[i].test(i) is always true.
  std::vector<SparseBitVector<>> Dominators;

  // If ForcedDepends[i].test(j) is true, in order for Nodes[i] to be outlined,
  // Nodes[j] must also be outlined.
  std::vector<SparseBitVector<>> ForcedDepends;

  // If DominatingDepends[i].test(j) is true, in order for Nodes[i] to be
  // outlined, either Nodes[j] must also be outlined, or Nodes[j] must dominate
  // the outlining point.
  std::vector<SparseBitVector<>> DominatingDepends;

  Function &F;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  // Wrapped in unique_ptr so OutliningDependenceResults can be moved.
  std::unique_ptr<CorrectPostDominatorTree> CPDT;
  MemorySSA &MSSA;

private:
  std::optional<size_t> lookupNode(Value *V);
  void addDepend(Value *User, Value *Def, bool is_data_dependency = false);
  void addForcedDepend(Value *User, Value *Def);
  void numberNodes();
  void analyzeBlock(BasicBlock *BB);
  void analyzeMemoryPhi(MemoryPhi *MPhi);
  void analyzeInstruction(Instruction *I);
  void finalizeDepends();
};

class OutliningDependenceAnalysis
    : public AnalysisInfoMixin<OutliningDependenceAnalysis> {
  friend AnalysisInfoMixin<OutliningDependenceAnalysis>;

  static AnalysisKey Key;

public:
  using Result = OutliningDependenceResults;

  OutliningDependenceResults run(Function &f, FunctionAnalysisManager &am);
};

class OutliningDependencePrinterPass
    : public PassInfoMixin<OutliningDependencePrinterPass> {
  raw_ostream &os;

public:
  explicit OutliningDependencePrinterPass(raw_ostream &os) : os(os) {}

  PreservedAnalyses run(Function &f, FunctionAnalysisManager &am);
};

struct OutliningDependenceWrapperPass : public FunctionPass {
  OutliningDependenceWrapperPass();

  static char ID;

  bool runOnFunction(Function &F) override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override;
  void verifyAnalysis() const override;
  OutliningDependenceResults &getOutDep() { return *OutDep; }

private:
  Optional<OutliningDependenceResults> OutDep;
};

} // end namespace bcdb

#endif // BCDB_OUTLINING_DEPENDENCE_H
