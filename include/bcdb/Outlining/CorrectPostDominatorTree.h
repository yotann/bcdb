#ifndef BCDB_CORRECTPOSTDOMINATORTREE_H
#define BCDB_CORRECTPOSTDOMINATORTREE_H

// LLVM's normal control-flow graph and PostDominatorTree are actually
// incorrect; they ignore the implicit control flow that happens when an
// instruction throws an exception or exits the program. If we relied on them
// for outlining, we might effectively move an instruction after a throw/exit
// before the throw/exit, changing the behavior of the program.
//
// This file provides CorrectPostDominatorTree, which is like PostDominatorTree
// except that it accounts for implicit control flow. It effectively adds an
// "implicit node" to the CFG, and for each basic block containing a possible
// throw/exit, it adds an edge to the implicit node. Then it uses LLVM's
// standard PostDominatorTree calculation code.
//
// Note that this code still works on the basic block level. Within a basic
// block, instructions that come after a throw/exit have a control dependence
// on it, but those dependences will have to be handled separately.

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/GraphTraits.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/GenericDomTree.h>
#include <llvm/Support/GenericDomTreeConstruction.h>
#include <llvm/Support/raw_ostream.h>
#include <vector>

namespace bcdb {

class CorrectCFG;

class CorrectCFGNode {
public:
  using iterator = std::vector<CorrectCFGNode *>::iterator;

  // The bb argument is nullptr if this is the implicit node.
  CorrectCFGNode(CorrectCFG *parent, llvm::BasicBlock *bb)
      : parent(parent), bb(bb) {
    if (isImplicitNode())
      return;
    for (llvm::Instruction &ins : *bb) {
      if (ins.isTerminator() && ins.getNumSuccessors() == 0) {
        // isGuaranteedToTransferExecutionToSuccessor returns false for
        // these, since they have no successors, but we don't care about
        // them because they don't create any control dependences.
        continue;
      }

      // We don't have to worry about instructions that might trap (divide by
      // 0, load from invalid address, etc.), because that's undefined
      // behavior, and we can do whatever we want if there's undefined
      // behavior.
      //
      // We also don't have to worry about volatile memory accesses, because
      // the LLVM Language Reference says: "The compiler may assume execution
      // will continue after a volatile operation, so operations which modify
      // memory or may have undefined behavior can be hoisted past a volatile
      // operation."
      //
      // We only have to worry about instructions that might throw an exception
      // or exit the program, which is exactly what
      // isGuaranteedToTransferExecutionToSuccessor() checks for.
      if (!llvm::isGuaranteedToTransferExecutionToSuccessor(&ins)) {
        has_implicit_control_flow = true;
        return;
      }
    }
  }

  bool isImplicitNode() const { return !bb; }

  CorrectCFG *getParent() { return parent; }

  void printAsOperand(llvm::raw_ostream &os, bool print_type) const {
    if (isImplicitNode())
      os << "<implicit node>";
    else
      bb->printAsOperand(os, print_type);
  }

  void calculateEdges();

  CorrectCFG *parent;
  llvm::BasicBlock *bb;
  std::vector<CorrectCFGNode *> pred_nodes, succ_nodes;
  bool has_implicit_control_flow = false;
};

class CorrectCFG {
public:
  using iterator = std::vector<CorrectCFGNode>::iterator;

  // Moving a CorrectCFG would break all the pointers.
  CorrectCFG(const CorrectCFG &) = delete;
  CorrectCFG(CorrectCFG &&) = delete;
  CorrectCFG &operator=(const CorrectCFG &) = delete;
  CorrectCFG &operator=(CorrectCFG &&) = delete;

  explicit CorrectCFG(llvm::Function &func) : func(func) {
    nodes.emplace_back(this, nullptr);
    for (llvm::BasicBlock &bb : func) {
      node_indices[&bb] = nodes.size();
      nodes.emplace_back(this, &bb);
    }
    for (CorrectCFGNode &node : nodes)
      node.calculateEdges();
  }

  CorrectCFGNode *getNodeFor(llvm::BasicBlock *bb) {
    return &nodes[node_indices[bb]];
  }

  CorrectCFGNode *getImplicitNode() { return &nodes[0]; }

  llvm::Function &func;
  std::vector<CorrectCFGNode> nodes;
  llvm::DenseMap<llvm::BasicBlock *, size_t> node_indices;
};

inline void CorrectCFGNode::calculateEdges() {
  if (isImplicitNode()) {
    for (CorrectCFGNode &other : parent->nodes)
      if (other.has_implicit_control_flow)
        pred_nodes.emplace_back(&other);
  } else {
    for (llvm::BasicBlock *target : successors(bb))
      succ_nodes.emplace_back(parent->getNodeFor(target));
    for (llvm::BasicBlock *target : predecessors(bb))
      pred_nodes.emplace_back(parent->getNodeFor(target));
    if (has_implicit_control_flow)
      succ_nodes.emplace_back(parent->getImplicitNode());
  }
}

} // end namespace bcdb

template <> struct llvm::GraphTraits<bcdb::CorrectCFGNode *> {
  using NodeRef = bcdb::CorrectCFGNode *;
  using ChildIteratorType = bcdb::CorrectCFGNode::iterator;

  static ChildIteratorType child_begin(NodeRef node) {
    return node->succ_nodes.begin();
  }

  static ChildIteratorType child_end(NodeRef node) {
    return node->succ_nodes.end();
  }
};

template <> struct llvm::GraphTraits<llvm::Inverse<bcdb::CorrectCFGNode *>> {
  using NodeRef = bcdb::CorrectCFGNode *;
  using ChildIteratorType = bcdb::CorrectCFGNode::iterator;

  static ChildIteratorType child_begin(NodeRef node) {
    return node->pred_nodes.begin();
  }

  static ChildIteratorType child_end(NodeRef node) {
    return node->pred_nodes.end();
  }
};

template <>
struct llvm::GraphTraits<bcdb::CorrectCFG *>
    : public llvm::GraphTraits<bcdb::CorrectCFGNode *> {
  static NodeRef getEntryNode(bcdb::CorrectCFG *cfg) {
    return cfg->getNodeFor(&cfg->func.getEntryBlock());
  }

  using nodes_iterator = pointer_iterator<bcdb::CorrectCFG::iterator>;

  static nodes_iterator nodes_begin(bcdb::CorrectCFG *cfg) {
    return nodes_iterator(cfg->nodes.begin());
  }

  static nodes_iterator nodes_end(bcdb::CorrectCFG *cfg) {
    return nodes_iterator(cfg->nodes.end());
  }
};

namespace bcdb {

class CorrectPostDominatorTree : public llvm::PostDomTreeBase<CorrectCFGNode> {
public:
  using Base = llvm::PostDomTreeBase<CorrectCFGNode>;
  explicit CorrectPostDominatorTree(llvm::Function &func) : cfg(func) {
    recalculate(cfg);
  }

  using Base::operator[];
  using Base::properlyDominates;

  llvm::DomTreeNodeBase<CorrectCFGNode> *operator[](llvm::BasicBlock *bb) {
    return operator[](cfg.getNodeFor(bb));
  }

  bool properlyDominates(llvm::BasicBlock *a, llvm::BasicBlock *b) {
    return properlyDominates(cfg.getNodeFor(a), cfg.getNodeFor(b));
  }

private:
  CorrectCFG cfg;
};

} // end namespace bcdb

#endif // BCDB_CORRECTPOSTDOMINATORTREE_H
