#ifndef BCDB_OUTLINING_DEPENDENCE_H
#define BCDB_OUTLINING_DEPENDENCE_H

#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/Pass.h>
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

  bool isOutlinable(const BitVector &BV) const;

  void getExternals(const BitVector &BV, BitVector &ArgInputs,
                    BitVector &ExternalInputs, BitVector &ExternalOutputs);

  // Each node must be one of the following types:
  // - Instruction
  // - BasicBlock, used before instructions to represent control dependencies
  // - MemoryPhi, used immediately after BasicBlock
  std::vector<Value *> Nodes;

  // We guarantee Nodes[NodeIndices[V]] == V.
  DenseMap<Value *, ssize_t> NodeIndices;

  BitVector PreventsOutlining;

  std::vector<BitVector> DataDepends;
  std::vector<BitVector> ArgDepends;

  // If Dominators[i][j] is true, Nodes[i] is dominated by Nodes[j]. Note that
  // nodes dominate themselves--Dominators[i][i] is always true.
  std::vector<BitVector> Dominators;

  // If ForcedDepends[i][j] is true, in order for Nodes[i] to be outlined,
  // Nodes[j] must also be outlined.
  std::vector<BitVector> ForcedDepends;

  // If DominatingDepends[i][j] is true, in order for Nodes[i] to be outlined,
  // either Nodes[j] must also be outlined, or Nodes[j] must dominate the
  // outlining point.
  std::vector<BitVector> DominatingDepends;

  Function &F;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  MemorySSA &MSSA;

private:
  ssize_t lookupNode(Value *V);
  void addDepend(Value *Def, Value *User, bool data = false);
  void addForcedDepend(Value *Def, Value *User);
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
