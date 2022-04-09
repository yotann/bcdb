#ifndef MEMODB_JSONENCODER_H
#define MEMODB_JSONENCODER_H

#include <cstdint>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>

#include "NodeVisitor.h"

namespace memodb {

/// Visitor that prints Nodes in MemoDB JSON format.
class JSONEncoder : public NodeVisitor {
public:
  JSONEncoder(llvm::raw_ostream &os);
  virtual ~JSONEncoder();
  void visitNode(const Node &value) override;
  void visitNull() override;
  void visitBoolean(bool value) override;
  void visitUInt64(std::uint64_t value) override;
  void visitInt64(std::int64_t value) override;
  void visitFloat(double value) override;
  void visitString(llvm::StringRef value) override;
  void visitBytes(BytesRef value) override;
  void visitLink(const Link &value) override;
  void startList(const Node::List &value) override;
  void endList() override;
  void startMap(const Node::Map &value) override;
  void visitKey(llvm::StringRef value) override;
  void endMap() override;

protected:
  llvm::raw_ostream &os;
  bool first = true;
};

} // end namespace memodb

#endif // MEMODB_JSONENCODER_H
