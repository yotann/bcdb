#ifndef MEMODB_NODEVISITOR_H
#define MEMODB_NODEVISITOR_H

#include <cstdint>

#include <llvm/ADT/StringRef.h>

#include "CID.h"
#include "Node.h"

namespace memodb {

/// Visitor for various kinds of Nodes.
///
/// TODO: profile this. If it's too slow, switch to the Curiously Recurring
/// Template Pattern.
class NodeVisitor {
public:
  virtual ~NodeVisitor();
  virtual void visitNode(const Node &value);
  virtual void visitNull();
  virtual void visitBoolean(bool value);
  virtual void visitUInt64(std::uint64_t value);
  virtual void visitInt64(std::int64_t value);
  virtual void visitFloat(double value);
  virtual void visitString(llvm::StringRef value);
  virtual void visitBytes(BytesRef value);
  virtual void visitList(const Node::List &value);
  virtual void visitMap(const Node::Map &value);
  virtual void visitLink(const CID &value);
  virtual void startList(const Node::List &value);
  virtual void endList();
  virtual void startMap(const Node::Map &value);
  virtual void visitKey(llvm::StringRef value);
  virtual void endMap();
};

} // end namespace memodb

#endif // MEMODB_NODEVISITOR_H
