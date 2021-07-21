#ifndef MEMODB_STORE_H
#define MEMODB_STORE_H

#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <stddef.h>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include "CID.h"
#include "Node.h"

namespace memodb {

struct Head {
  std::string Name;

  explicit Head(const char *Name) : Name(Name) {}
  explicit Head(std::string &&Name) : Name(Name) {}
  explicit Head(llvm::StringRef Name) : Name(Name) {}
};

std::ostream &operator<<(std::ostream &os, const Head &head);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Head &head);

struct Call {
  std::string Name;
  std::vector<CID> Args;

  Call(llvm::StringRef Name, llvm::ArrayRef<CID> Args)
      : Name(Name), Args(Args) {}
};

std::ostream &operator<<(std::ostream &os, const Call &call);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Call &call);

struct Name : public std::variant<CID, Head, Call> {
  typedef std::variant<CID, Head, Call> BaseType;

  constexpr Name(const CID &Ref) : variant(Ref) {}
  constexpr Name(const Head &Head) : variant(Head) {}
  constexpr Name(const Call &Call) : variant(Call) {}
  constexpr Name(CID &&Ref) : variant(Ref) {}
  constexpr Name(Head &&Head) : variant(Head) {}
  constexpr Name(Call &&Call) : variant(Call) {}

  template <class Visitor> constexpr void visit(Visitor &&vis) {
    BaseType &Base = *this;
    return std::visit(vis, Base);
  }

  template <class Visitor> constexpr void visit(Visitor &&vis) const {
    const BaseType &Base = *this;
    return std::visit(vis, Base);
  }
};

std::ostream &operator<<(std::ostream &os, const Name &name);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Name &name);

using Path = std::pair<Name, std::vector<Node>>;

class Store;

class NodeOrCID : private std::variant<CID, Node> {
public:
  constexpr NodeOrCID(const CID &cid) : variant(cid) {}
  constexpr NodeOrCID(const Node &node) : variant(node) {}
  constexpr NodeOrCID(CID &&cid) : variant(std::move(cid)) {}
  constexpr NodeOrCID(Node &&node) : variant(std::move(node)) {}

private:
  friend class NodeRef;
  typedef std::variant<CID, Node> BaseType;
};

class NodeRef {
  Store &store;
  std::optional<CID> cid = std::nullopt;
  std::optional<Node> node = std::nullopt;

public:
  NodeRef(Store &store, const NodeRef &other);
  NodeRef(Store &store, const NodeOrCID &node_or_cid);
  NodeRef(Store &store, const CID &cid);
  NodeRef(Store &store, const CID &cid, const Node &node);

  const Node &operator*();
  const Node *operator->();
  const CID &getCID();
};

class Store {
public:
  static std::unique_ptr<Store> open(llvm::StringRef uri,
                                     bool create_if_missing = false);

  virtual ~Store() {}

  virtual llvm::Optional<Node> getOptional(const CID &CID) = 0;
  virtual llvm::Optional<CID> resolveOptional(const Name &Name) = 0;
  virtual CID put(const Node &value) = 0;
  virtual void set(const Name &Name, const CID &ref) = 0;
  virtual std::vector<Name> list_names_using(const CID &ref) = 0;
  virtual std::vector<std::string> list_funcs() = 0;
  // F should not modify the database. F can return true to stop iteration.
  virtual void eachHead(std::function<bool(const Head &)> F) = 0;
  virtual void eachCall(llvm::StringRef Func,
                        std::function<bool(const Call &)> F) = 0;
  virtual void head_delete(const Head &Head) = 0;
  virtual void call_invalidate(llvm::StringRef name) = 0;

  virtual bool has(const CID &CID);
  virtual bool has(const Name &Name);

  Node get(const CID &CID);
  CID resolve(const Name &Name);

  std::vector<Head> list_heads();
  std::vector<Call> list_calls(llvm::StringRef Func);
  std::vector<Path> list_paths_to(const CID &ref);
};

} // end namespace memodb

#endif // MEMODB_MEMODB_H
