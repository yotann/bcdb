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

/// Refers to a named head in the Store.
struct Head {
  std::string Name;

  explicit Head(const char *Name) : Name(Name) {}
  explicit Head(std::string &&Name) : Name(Name) {}
  explicit Head(llvm::StringRef Name) : Name(Name) {}
};

std::ostream &operator<<(std::ostream &os, const Head &head);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Head &head);

/// Refers to a call in the Store, with the func name and arguments.
struct Call {
  std::string Name;
  std::vector<CID> Args;

  Call(llvm::StringRef Name, llvm::ArrayRef<CID> Args)
      : Name(Name), Args(Args) {}

  bool operator<(const Call &other) const;
  bool operator==(const Call &other) const;
  bool operator!=(const Call &other) const;
};

std::ostream &operator<<(std::ostream &os, const Call &call);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Call &call);

// A CID, Head, or Call.
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

  static std::optional<Name> parse(llvm::StringRef uri_str);
};

std::ostream &operator<<(std::ostream &os, const Name &name);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Name &name);

using Path = std::pair<Name, std::vector<Node>>;

class Store;

/// Either a Node or a CID referring to a Node. This class is used as the
/// return value of functions called by Evaluator, which will normally return a
/// Node, but have the option of returning a CID instead if that would be more
/// efficient.
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

/// Refers to a Node in a Store that may or may not be loaded in memory. If the
/// Node is not yet loaded in memory, it will be automatically loaded from the
/// Store when needed.
class NodeRef {
  Store &store;
  std::optional<CID> cid = std::nullopt;
  std::optional<Node> node = std::nullopt;

public:
  NodeRef(Store &store, const NodeRef &other);
  NodeRef(Store &store, const NodeOrCID &node_or_cid);
  NodeRef(Store &store, const CID &cid);
  NodeRef(Store &store, const CID &cid, const Node &node);

  /// Fetch the Node, if necessary, and return a reference to it.
  const Node &operator*();

  /// Fetch the Node, if necessary, and access a member of it.
  const Node *operator->();

  /// Get the CID of the Node.
  const CID &getCID();

  /// Free the stored Node, if any. Useful to reduce memory usage.
  void freeNode();
};

/// A MemoDB store, containing Nodes, Heads, and Calls. The store may be backed
/// by some sort of database, or it may be backed by a separately running
/// server.
class Store {
public:
  /// Open a Store.
  ///
  /// If the Store cannot be accessed successfully, this function will abort
  /// the program.
  ///
  /// \param uri The URI of the store to open. Supported schemes may include
  /// `sqlite:`, `rocksdb:`, `car:`, and `http:`.
  ///
  /// \param create_if_missing If true, and the URI refers to a nonexistent
  /// file, create a new empty database there.
  static std::unique_ptr<Store> open(llvm::StringRef uri,
                                     bool create_if_missing = false);

  virtual ~Store() {}

  /// Get a Node by its CID.
  virtual llvm::Optional<Node> getOptional(const CID &CID) = 0;

  /// Resolve a Head or Call to the stored CID.
  virtual llvm::Optional<CID> resolveOptional(const Name &Name) = 0;

  /// Add a Node.
  virtual CID put(const Node &value) = 0;

  /// Change the CID stored for a Head or Call.
  virtual void set(const Name &Name, const CID &ref) = 0;

  /// List all CIDs, Heads, and Calls that refer to the specified Node.
  virtual std::vector<Name> list_names_using(const CID &ref) = 0;

  /// List all funcs that have cached results in the store.
  virtual std::vector<std::string> list_funcs() = 0;

  /// Call a function for each Head in the store. @p F should not modify the
  /// database. @p F can return true to stop iteration.
  virtual void eachHead(std::function<bool(const Head &)> F) = 0;

  /// Call a function for each Call of the specified func in the store. @p F
  /// should not modify the database. @p F can return true to stop iteration.
  virtual void eachCall(llvm::StringRef Func,
                        std::function<bool(const Call &)> F) = 0;

  /// Delete a Head from the store.
  virtual void head_delete(const Head &Head) = 0;

  /// Delete all cached result for a given func.
  virtual void call_invalidate(llvm::StringRef name) = 0;

  /// Check whether a node with the given CID is present in the store.
  virtual bool has(const CID &CID);

  /// Check whether the given Head or Call is present in the store.
  virtual bool has(const Name &Name);

  /// Get a Node by its CID, aborting if it's missing.
  Node get(const CID &CID);

  /// Resolve a Head or Call to the stored CID, aborting if it's missing.
  CID resolve(const Name &Name);

  /// List all Heads in the store.
  std::vector<Head> list_heads();

  /// List all cached Calls of a given func in the store.
  std::vector<Call> list_calls(llvm::StringRef Func);

  /// Recursively call list_names_using() to find all paths to a given Node.
  std::vector<Path> list_paths_to(const CID &ref);
};

} // end namespace memodb

#endif // MEMODB_MEMODB_H
