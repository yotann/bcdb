#include "memodb/NodeVisitor.h"

#include <variant>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"

using namespace memodb;

using std::int64_t;
using std::uint64_t;

NodeVisitor::~NodeVisitor() {}

void NodeVisitor::visitNode(const Node &value) {
  std::visit(
      Overloaded{
          [this](const std::monostate &) { visitNull(); },
          [this](const bool &value) { visitBoolean(value); },
          [this](const int64_t &value) { visitInt64(value); },
          [this](const uint64_t &value) { visitUInt64(value); },
          [this](const double &value) { visitFloat(value); },
          [this](const Node::StringStorage &value) { visitString(value); },
          [this](const Node::BytesStorage &value) { visitBytes(value); },
          [this](const Node::List &value) { visitList(value); },
          [this](const Node::Map &value) { visitMap(value); },
          [this](const Link &value) { visitLink(value); },
      },
      value.variant_);
}

void NodeVisitor::visitNull() {}

void NodeVisitor::visitBoolean(bool value) {}

void NodeVisitor::visitUInt64(std::uint64_t value) {}

void NodeVisitor::visitInt64(std::int64_t value) {}

void NodeVisitor::visitFloat(double value) {}

void NodeVisitor::visitString(llvm::StringRef value) {}

void NodeVisitor::visitBytes(BytesRef value) {}

void NodeVisitor::visitList(const Node::List &value) {
  startList(value);
  for (const Node &item : value)
    visitNode(item);
  endList();
}

void NodeVisitor::visitMap(const Node::Map &value) {
  startMap(value);
  for (const auto &item : value) {
    visitKey(item.key());
    visitNode(item.value());
  }
  endMap();
}

void NodeVisitor::visitLink(const Link &value) {}

void NodeVisitor::startList(const Node::List &value) {}

void NodeVisitor::endList() {}

void NodeVisitor::startMap(const Node::Map &value) {}

void NodeVisitor::visitKey(llvm::StringRef value) { visitString(value); }

void NodeVisitor::endMap() {}
