#ifndef MEMODB_JSONENCODER_H
#define MEMODB_JSONENCODER_H

#include <cstdint>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>

#include "CID.h"
#include "NodeVisitor.h"

namespace memodb {

/// Visitor that prints Nodes in MemoDB JSON format.
class JSONEncoder : public NodeVisitor {
public:
  JSONEncoder(llvm::raw_ostream &os);
  virtual ~JSONEncoder();
  virtual void visitNode(const Node &value) override;
  virtual void visitNull() override;
  virtual void visitBoolean(bool value) override;
  virtual void visitUInt64(std::uint64_t value) override;
  virtual void visitInt64(std::int64_t value) override;
  virtual void visitFloat(double value) override;
  virtual void visitString(llvm::StringRef value) override;
  virtual void visitBytes(BytesRef value) override;
  virtual void visitLink(const CID &value) override;
  virtual void startList(const Node::List &value) override;
  virtual void endList() override;
  virtual void startMap(const Node::Map &value) override;
  virtual void visitKey(llvm::StringRef value) override;
  virtual void endMap() override;

private:
  llvm::raw_ostream &os;
  bool first = true;
};

} // end namespace memodb

#endif // MEMODB_JSONENCODER_H
