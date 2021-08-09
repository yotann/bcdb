#ifndef MEMODB_NODE_H
#define MEMODB_NODE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "CID.h"
#include "Support.h"

namespace memodb {

class Node;

using BytesRef = llvm::ArrayRef<std::uint8_t>;

// An alternative to std::pair for holding key-value pairs.
template <class KeyT, class ValueT> class KeyValue {
private:
  KeyT key_;
  ValueT value_;

public:
  KeyValue(const KeyT &key, const ValueT &value) : key_(key), value_(value) {}
  KeyValue(const KeyT &key) : key_(key), value_() {}
  const KeyT &key() const { return key_; }
  ValueT &value() { return value_; }
  const ValueT &value() const { return value_; }
  bool operator==(const KeyValue &other) const {
    return key_ == other.key_ && value_ == other.value_;
  }
  bool operator!=(const KeyValue &other) const { return !(*this == other); }
  bool operator<(const KeyValue &other) const {
    return key_ != other.key_ ? key_ < other.key_ : value_ < other.value_;
  }
};

// A map implementation based on a sorted vector (like the one in jsoncons).
template <class KeyT, class ValueT> class NodeMap {
public:
  using key_value_type = KeyValue<KeyT, ValueT>;

private:
  using container_type = std::vector<key_value_type>;
  container_type members_;
  static bool compare(const key_value_type &a, const llvm::StringRef &b) {
    if (a.key().size() != b.size())
      return a.key().size() < b.size();
    return a.key() < b;
  }

public:
  using iterator = typename container_type::iterator;
  using const_iterator = typename container_type::const_iterator;
  NodeMap() = default;
  NodeMap(const NodeMap &other) = default;
  NodeMap(NodeMap &&other) = default;
  NodeMap &operator=(const NodeMap &other) = default;
  NodeMap &operator=(NodeMap &&other) = default;
  NodeMap(
      const std::initializer_list<std::pair<llvm::StringRef, ValueT>> &init) {
    members_.reserve(init.size());
    for (auto &item : init)
      insert_or_assign(item.first, item.second);
  }
  bool empty() const { return members_.empty(); }
  iterator begin() { return members_.begin(); }
  iterator end() { return members_.end(); }
  const_iterator begin() const { return members_.begin(); }
  const_iterator end() const { return members_.end(); }
  std::size_t size() const { return members_.size(); }
  void clear() { members_.clear(); }
  void reserve(std::size_t n) { members_.reserve(n); }
  iterator find(const llvm::StringRef &name) noexcept {
    auto it = std::lower_bound(members_.begin(), members_.end(), name, compare);
    return it != members_.end() && it->key() == name ? it : members_.end();
  }
  const_iterator find(const llvm::StringRef &name) const noexcept {
    auto it = std::lower_bound(members_.begin(), members_.end(), name, compare);
    return it != members_.end() && it->key() == name ? it : members_.end();
  }
  template <class T>
  std::pair<iterator, bool> insert_or_assign(const llvm::StringRef &name,
                                             T &&value) {
    auto it = std::lower_bound(members_.begin(), members_.end(), name, compare);
    if (it != members_.end() && it->key() == name) {
      it->value() = std::forward<T>(value);
      return std::make_pair(it, false);
    }
    it = members_.emplace(it, KeyT(name.begin(), name.end()),
                          std::forward<T>(value));
    return std::make_pair(it, true);
  }
  template <class... Args>
  std::pair<iterator, bool> try_emplace(const llvm::StringRef &name,
                                        Args &&...args) {
    auto it = std::lower_bound(members_.begin(), members_.end(), name, compare);
    if (it != members_.end() && it->key() == name)
      return std::make_pair(it, false);
    it = members_.emplace(it, KeyT(name.begin(), name.end()),
                          std::forward<Args>(args)...);
    return std::make_pair(it, true);
  }
  bool operator==(const NodeMap &other) const {
    return members_ == other.members_;
  }
  bool operator!=(const NodeMap &other) const {
    return members_ != other.members_;
  }
  bool operator<(const NodeMap &other) const {
    return members_ < other.members_;
  }
};

// A range consisting of two iterators.
template <class IteratorT> class Range {
  IteratorT first_, last_;

public:
  using iterator = IteratorT;
  Range(const IteratorT &first, const IteratorT &last)
      : first_(first), last_(last) {}
  iterator begin() { return first_; }
  iterator end() { return last_; }
};

struct NodeListArg {
  explicit NodeListArg() = default;
};
constexpr NodeListArg node_list_arg{};

struct NodeMapArg {
  explicit NodeMapArg() = default;
};
constexpr NodeMapArg node_map_arg{};

struct UTF8StringArg {
  explicit UTF8StringArg() = default;
};
constexpr UTF8StringArg utf8_string_arg{};

struct ByteStringArg {
  explicit ByteStringArg() = default;
};
constexpr ByteStringArg byte_string_arg{};

// When specialized for a type, allows that type to be used with Node::as,
// Node::is, etc.
template <class T, class Enable = void> struct NodeTypeTraits {
  // Check whether the Node can be converted to type T.
  static constexpr bool is(const Node &) noexcept { return false; }

  // Convert the Node to type T, aborting if this is impossible.
  static T as(const Node &) {
    static_assert(Unimplemented<T>::value, "not implemented");
  }

  // Convert the Node to type T in bytestring mode, aborting if this is
  // impossible.
  static T as(const Node &, ByteStringArg) {
    static_assert(Unimplemented<T>::value, "not implemented");
  }
};

// The essential kinds of data in the IPLD Data Model:
// https://github.com/ipld/specs/blob/master/data-model-layer/data-model.md
enum class Kind {
  Null,
  Boolean,
  Integer,
  Float,
  String,
  Bytes,
  List,
  Map,
  Link,
};

// A value that may be stored in the IPLD Data Model:
// https://github.com/ipld/specs/blob/master/data-model-layer/data-model.md
//
// The API is based on jsoncons::basic_json:
// https://github.com/danielaparker/jsoncons/blob/master/include/jsoncons/basic_json.hpp
// Note that some terminology is different (jsoncons "object" is our "Map",
// jsoncons "array" is our "List").
class Node {

public:
  using reference = Node &;
  using const_reference = const Node &;
  using pointer = Node *;
  using const_pointer = const Node *;

  using List = std::vector<Node>;
  using Map = NodeMap<llvm::SmallString<12>, Node>;

private:
  using BytesStorage = llvm::SmallVector<std::uint8_t, 48>;
  using StringStorage = llvm::SmallString<48>;

  // The monostate represents null.
  std::variant<std::monostate, bool, std::int64_t, double, BytesStorage,
               StringStorage, CID, List, Map>
      variant_;

  void validateUTF8() const;
  void validateKeysUTF8() const;

  friend std::ostream &operator<<(std::ostream &, const Node &);
  friend struct NodeTypeTraits<bool>;
  friend struct NodeTypeTraits<std::int64_t>;
  friend struct NodeTypeTraits<double>;
  friend struct NodeTypeTraits<BytesRef>;
  friend struct NodeTypeTraits<llvm::StringRef>;
  friend struct NodeTypeTraits<CID>;

public:
  // Constructors and assignment.

  // NOTE: default is null (jsoncons default is empty map).
  Node();
  Node(const Node &Other) = default;
  Node(Node &&Other) noexcept = default;
  Node &operator=(const Node &Other) = default;
  Node &operator=(Node &&Other) noexcept = default;

  Node(std::nullptr_t);
  Node &operator=(std::nullptr_t);

  Node(bool val);

  template <typename IntegerType,
            std::enable_if_t<std::is_integral<IntegerType>::value &&
                                 std::is_unsigned<IntegerType>::value &&
                                 sizeof(IntegerType) < sizeof(std::int64_t),
                             int> = 0>
  Node(IntegerType val) : variant_(std::int64_t(val)) {}

  template <typename IntegerType,
            std::enable_if_t<std::is_integral<IntegerType>::value &&
                                 std::is_unsigned<IntegerType>::value &&
                                 sizeof(IntegerType) == sizeof(std::int64_t),
                             int> = 0>
  Node(IntegerType val) {
    if (val > (IntegerType)std::numeric_limits<std::int64_t>::max())
      llvm::report_fatal_error("Integer overflow");
    variant_ = std::int64_t(val);
  }

  template <typename IntegerType,
            std::enable_if_t<std::is_integral<IntegerType>::value &&
                                 std::is_signed<IntegerType>::value &&
                                 sizeof(IntegerType) <= sizeof(std::int64_t),
                             int> = 0>
  Node(IntegerType val) : variant_(std::int64_t(val)) {}

  Node(double Float);

  Node(const char *Str);
  Node(const char *Str, std::size_t Length);
  Node &operator=(const char *Str);

  // We require an explicit utf8_string_arg argument because strings are often
  // not valid UTF-8.
  Node(UTF8StringArg, const llvm::StringRef &sr);
  Node(UTF8StringArg, const std::string_view &sv);
  Node(UTF8StringArg, const std::string &str);

  Node(ByteStringArg);

  // FIXME: Check whether Source really is a byte/char sequence.
  template <class Source>
  Node(ByteStringArg, const Source &source)
      : variant_(
            BytesStorage(reinterpret_cast<const std::uint8_t *>(source.data()),
                         reinterpret_cast<const std::uint8_t *>(source.data()) +
                             source.size())) {}

  Node(BytesRef bytes);

  explicit Node(NodeListArg);

  template <class InputIt>
  Node(NodeListArg, InputIt first, InputIt last) : Node(List(first, last)) {}

  Node(NodeListArg, std::initializer_list<Node> init);

  Node(const List &list);
  Node(List &&list);

  explicit Node(NodeMapArg);

  template <class InputIt>
  Node(NodeMapArg, InputIt first, InputIt last) : Node(Map(first, last)) {
    validateKeysUTF8();
  }

  Node(NodeMapArg,
       std::initializer_list<std::pair<llvm::StringRef, Node>> init);

  Node(const Map &map);
  Node(Map &&map);

  // Explicit so we don't confuse Node values with links when setting up e.g.
  // call arguments.
  explicit Node(const CID &val);
  explicit Node(CID &&val);

  ~Node() noexcept {}

  // Comparison operators.

  bool operator==(const Node &other) const;
  bool operator!=(const Node &other) const;
  bool operator<(const Node &other) const;

  // Saving and loading.

  // Load a Node from DAG-CBOR bytes:
  // https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-cbor.md
  static llvm::Expected<Node> loadFromCBOR(BytesRef in);

  // Load a Node from DAG-CBOR bytes at the beginning of a sequence. When this
  // returns, "in" will refer to the rest of the bytes after the DAG-CBOR
  // value.
  static llvm::Expected<Node> loadFromCBORSequence(BytesRef &in);

  // Save a Node to DAG-CBOR bytes.
  void save_cbor(std::vector<std::uint8_t> &out) const;

  // Load a Node from a CID and the corresponding content bytes. The CID
  // content type may be either Raw (bytes returned as a bytestring Node) or
  // DAG-CBOR. The CID hash type may be Identity, in which case the value is
  // loaded directly from the CID.
  static llvm::Expected<Node> loadFromIPLD(const CID &CID, BytesRef Content);

  // Save a Node as a CID and the corresponding content bytes. The CID content
  // type will be either Raw (if this is a bytestring Node) or DAG-CBOR. If
  // noIdentity is false and the value is small enough, the CID will be an
  // Identity CID and the returned content bytes will be empty.
  std::pair<CID, std::vector<std::uint8_t>>
  saveAsIPLD(bool noIdentity = false) const;

  // Load a Node from the MemoDB JSON format.
  static llvm::Expected<Node> loadFromJSON(llvm::StringRef json);

  // Checking the kind of Node.

  Kind kind() const;

  constexpr bool is_null() const noexcept {
    return std::holds_alternative<std::monostate>(variant_);
  }

  constexpr bool is_boolean() const noexcept {
    return std::holds_alternative<bool>(variant_);
  }

  constexpr bool is_integer() const noexcept {
    return std::holds_alternative<std::int64_t>(variant_);
  }

  constexpr bool is_float() const noexcept {
    return std::holds_alternative<double>(variant_);
  }

  constexpr bool is_number() const noexcept {
    return is_integer() || is_float();
  }

  constexpr bool is_string() const noexcept {
    return std::holds_alternative<StringStorage>(variant_);
  }

  constexpr bool is_bytes() const noexcept {
    return std::holds_alternative<BytesStorage>(variant_);
  }

  constexpr bool is_list() const noexcept {
    return std::holds_alternative<List>(variant_);
  }

  constexpr bool is_map() const noexcept {
    return std::holds_alternative<Map>(variant_);
  }

  constexpr bool is_link() const noexcept {
    return std::holds_alternative<CID>(variant_);
  }

  // All-purpose accessors using NodeTypeTraits.

  // Check whether this Node can be converted to type T.
  template <class T> constexpr bool is() const {
    return NodeTypeTraits<T>::is(*this);
  }

  // Convert this Node to type T, aborting if this is impossible.
  template <class T> T as() const { return NodeTypeTraits<T>::as(*this); }

  // Convert this Node to type T in bytestring mode, aborting if this is
  // impossible.
  template <class T> T as(ByteStringArg) const {
    return NodeTypeTraits<T>::as(*this, byte_string_arg);
  }

  // Convenience functions for Lists and Maps.

  // NOTE: works on strings and bytes, unlike jsoncons.
  std::size_t size() const;

  Node &operator[](std::size_t i);
  const Node &operator[](std::size_t i) const;
  Node &operator[](const llvm::StringRef &name);
  const Node &operator[](const llvm::StringRef &name) const;

  bool contains(const llvm::StringRef &key) const noexcept;

  std::size_t count(const llvm::StringRef &key) const;

  bool empty() const noexcept;

  void resize(std::size_t n);

  template <class T> void resize(std::size_t n, T val) {
    if (List *value = std::get_if<List>(&variant_))
      value->resize(n, val);
  }

  Node &at(const llvm::StringRef &name);
  const Node &at(const llvm::StringRef &name) const;

  // Stricter than jsoncons (maps don't work).
  Node &at(std::size_t i);

  // Stricter than jsoncons (maps don't work).
  const Node &at(std::size_t i) const;

  Map::iterator find(const llvm::StringRef &name);
  Map::const_iterator find(const llvm::StringRef &name) const;
  const Node &at_or_null(const llvm::StringRef &name) const;

  template <class T, class U>
  T get_value_or(const llvm::StringRef &name, U &&default_value) const {
    if (is_null())
      return static_cast<T>(std::forward<U>(default_value));
    const Map &value = std::get<Map>(variant_);
    auto iter = value.find(name);
    return iter == value.end() ? static_cast<T>(std::forward<U>(default_value))
                               : iter->value();
  }

  void clear();

  template <class T>
  std::pair<Map::iterator, bool> insert_or_assign(const llvm::StringRef &name,
                                                  T &&val) {
    return std::get<Map>(variant_).insert_or_assign(name, std::forward<T>(val));
  }

  template <class... Args>
  std::pair<Map::iterator, bool> try_emplace(const llvm::StringRef &name,
                                             Args &&...args) {
    return std::get<Map>(variant_).try_emplace(name,
                                               std::forward<Args>(args)...);
  }

  template <class... Args> Node &emplace_back(Args &&...args) {
    return std::get<List>(variant_).emplace_back(std::forward<Args>(args)...);
  }

  template <class T> void push_back(T &&val) {
    std::get<List>(variant_).push_back(std::forward<T>(val));
  }

  Range<Map::iterator> map_range();
  Range<Map::const_iterator> map_range() const;
  Range<List::iterator> list_range();
  Range<List::const_iterator> list_range() const;

  // Extra functions (DAG-CBOR specific).

  // Call func for each CID found in this Node.
  template <typename T> void eachLink(T func) const {
    std::visit(Overloaded{
                   [&](const CID &Link) { func(Link); },
                   [&](const List &List) {
                     for (const auto &Item : List)
                       Item.eachLink(func);
                   },
                   [&](const Map &Map) {
                     for (const auto &Item : Map)
                       Item.value().eachLink(func);
                   },
                   [&](const auto &X) {},
               },
               variant_);
  }
};

// Print a Node in MemoDB's JSON format.
std::ostream &operator<<(std::ostream &os, const Node &value);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Node &value);

// Various basic types to be used with Node::is and Node::as.

template <> struct NodeTypeTraits<bool> {
  static constexpr bool is(const Node &node) noexcept {
    return node.is_boolean();
  }

  static bool as(const Node &node) { return std::get<bool>(node.variant_); }
};

template <> struct NodeTypeTraits<std::int64_t> {
  static constexpr bool is(const Node &node) noexcept {
    return node.is_integer();
  }

  static std::int64_t as(const Node &node) {
    return std::get<std::int64_t>(node.variant_);
  }
};

template <class IntegerType>
struct NodeTypeTraits<
    IntegerType,
    typename std::enable_if<std::is_integral<IntegerType>::value &&
                            std::is_unsigned<IntegerType>::value>::type> {
  static bool is(const Node &node) noexcept {
    if (!node.is<std::int64_t>())
      return false;
    const std::int64_t value = node.as<std::int64_t>();
    return value >= 0 &&
           static_cast<std::int64_t>(static_cast<IntegerType>(value)) == value;
  }

  static IntegerType as(const Node &node) {
    if (!is(node))
      llvm::report_fatal_error("Integer overflow or not an integer");
    return static_cast<IntegerType>(node.as<std::int64_t>());
  }
};

template <class IntegerType>
struct NodeTypeTraits<
    IntegerType,
    typename std::enable_if<std::is_integral<IntegerType>::value &&
                            std::is_signed<IntegerType>::value>::type> {
  static bool is(const Node &node) noexcept {
    if (!node.is<std::int64_t>())
      return false;
    const std::int64_t value = node.as<std::int64_t>();
    return value >= std::numeric_limits<IntegerType>::lowest() &&
           value <= std::numeric_limits<IntegerType>::max();
  }

  static IntegerType as(const Node &node) {
    if (!is(node))
      llvm::report_fatal_error("Integer overflow or not an integer");
    return static_cast<IntegerType>(node.as<std::int64_t>());
  }
};

template <> struct NodeTypeTraits<double> {
  static constexpr bool is(const Node &node) noexcept {
    return node.is_number();
  }

  static double as(const Node &node) {
    return std::visit(Overloaded{
                          [](const double &value) { return value; },
                          [](const std::int64_t &value) {
                            return static_cast<double>(value);
                          },
                          [](const auto &) {
                            llvm::report_fatal_error("Not a number");
                            return 0.0;
                          },
                      },
                      node.variant_);
  }
};

template <> struct NodeTypeTraits<BytesRef> {
  static constexpr bool is(const Node &node) noexcept {
    return node.is_bytes();
  }

  static BytesRef as(const Node &node, ByteStringArg = byte_string_arg) {
    return std::get<Node::BytesStorage>(node.variant_);
  }
};

template <> struct NodeTypeTraits<llvm::StringRef> {
  static constexpr bool is(const Node &node) noexcept {
    return node.is_string();
  }

  static llvm::StringRef as(const Node &node) {
    return std::get<Node::StringStorage>(node.variant_);
  }

  static llvm::StringRef as(const Node &node, ByteStringArg) {
    auto bytes = node.as<BytesRef>();
    return llvm::StringRef(reinterpret_cast<const char *>(bytes.data()),
                           bytes.size());
  }
};

template <> struct NodeTypeTraits<std::string> {
  static constexpr bool is(const Node &node) noexcept {
    return node.is_string();
  }

  static std::string as(const Node &node) {
    return node.as<llvm::StringRef>().str();
  }

  static std::string as(const Node &node, ByteStringArg) {
    auto bytes = node.as<BytesRef>();
    return std::string(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size());
  }
};

template <> struct NodeTypeTraits<CID> {
  static constexpr bool is(const Node &node) noexcept { return node.is_link(); }

  static const CID &as(const Node &node) {
    return std::get<CID>(node.variant_);
  }
};

} // end namespace memodb

#endif // MEMODB_NODE_H
