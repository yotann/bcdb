#ifndef BCDB_OUTLINING_DEPENDENCE_H
#define BCDB_OUTLINING_DEPENDENCE_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SparseBitVector.h>
#include <llvm/Pass.h>
#include <optional>
#include <vector>

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

// Like a program dependency graph, except:
// - we include separate nodes for BasicBlock headers and MemorySSA
// - we distinguish ForcedDepends from DominatingDepends
// - we can get better results when X would normally depend on Y by making X
//   depend on Z (a dominator of X) and Z depend on Y.
class OutliningDependenceResults {
public:
  OutliningDependenceResults(Function &F, DominatorTree &DT,
                             PostDominatorTree &PDT, MemorySSA &MSSA);

  void print(raw_ostream &OS) const;

  // Check whether a candidate can be legally outlined.
  bool isOutlinable(const SparseBitVector<> &BV) const;

  // Get the list of function arguments and other nodes that would need to be
  // passed to the candidate if it were outlined, and the list of nodes inside
  // the candidate that would need to have their results passed back to the
  // original function.
  void getExternals(const SparseBitVector<> &BV, SparseBitVector<> &ArgInputs,
                    SparseBitVector<> &ExternalInputs,
                    SparseBitVector<> &ExternalOutputs);

  // Each node must be one of the following types:
  // - Instruction
  // - BasicBlock, used before instructions to represent control dependencies
  // - MemoryPhi, used immediately after BasicBlock
  std::vector<Value *> Nodes;

  // We guarantee Nodes[NodeIndices[V]] == V.
  DenseMap<Value *, size_t> NodeIndices;

  // If PreventsOutlining.test(i) is true, Nodes[i] may never be outlined.
  SparseBitVector<> PreventsOutlining;

  // If DataDepends[i].test(j) is true, Nodes[i] has a data dependency on
  // Nodes[j]. Used by getExternals().
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
