#ifndef BCDB_UTIL_H
#define BCDB_UTIL_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/GraphTraits.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <memory>
#include <utility>

namespace llvm {
class GlobalValue;
class Module;
} // end namespace llvm

namespace bcdb {

llvm::SmallPtrSet<llvm::GlobalValue *, 8> FindGlobalReferences(
    llvm::GlobalValue *Root,
    llvm::SmallPtrSetImpl<llvm::GlobalValue *> *ForcedSameModule = nullptr);

class GlobalReferenceGraph {
public:
  using Node = std::pair<GlobalReferenceGraph *, llvm::GlobalValue *>;
  GlobalReferenceGraph(llvm::Module &M);
  llvm::SmallVector<Node, 0> Nodes;
  llvm::DenseMap<llvm::GlobalValue *, llvm::SmallVector<Node, 8>> Edges;
};

} // end namespace bcdb

namespace llvm {
template <> struct GraphTraits<bcdb::GlobalReferenceGraph *> {
  using GraphType = bcdb::GlobalReferenceGraph;
  using NodeRef = GraphType::Node;
  using ChildIteratorType = llvm::SmallVectorImpl<NodeRef>::iterator;
  using nodes_iterator = llvm::SmallVectorImpl<NodeRef>::iterator;
  static NodeRef getEntryNode(GraphType *G) {
    return GraphType::Node(G, nullptr);
  }
  static nodes_iterator nodes_begin(GraphType *G) { return G->Nodes.begin(); }
  static nodes_iterator nodes_end(GraphType *G) { return G->Nodes.end(); }
  static inline ChildIteratorType child_begin(NodeRef N) {
    if (!N.second)
      return N.first->Nodes.begin();
    return N.first->Edges[N.second].begin();
  }
  static inline ChildIteratorType child_end(NodeRef N) {
    if (!N.second)
      return N.first->Nodes.end();
    return N.first->Edges[N.second].end();
  }
};
} // end namespace llvm

namespace bcdb {
std::unique_ptr<llvm::Module> CloneModuleCorrectly(const llvm::Module &M);
std::unique_ptr<llvm::Module>
CloneModuleCorrectly(const llvm::Module &M, llvm::ValueToValueMapTy &VMap);
std::unique_ptr<llvm::Module> CloneModuleCorrectly(
    const llvm::Module &M, llvm::ValueToValueMapTy &VMap,
    llvm::function_ref<bool(const llvm::GlobalValue *)> ShouldCloneDefinition);
} // end namespace bcdb

#endif // BCDB_UTIL_H
