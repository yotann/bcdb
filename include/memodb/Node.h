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

/// \defgroup utility MemoDB utility classes

/// \defgroup core MemoDB core data classes

/// \ingroup utility
using BytesRef = llvm::ArrayRef<std::uint8_t>;

/// An alternative to std::pair for holding key-value pairs.
/// \ingroup utility
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

/// A map implementation based on a sorted vector (like the one in jsoncons).
/// \ingroup utility
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
  void erase(const llvm::StringRef &name) {
    auto it = find(name);
    if (it != members_.end())
      members_.erase(it);
  }
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

/// A range consisting of two iterators.
/// \ingroup utility
template <class IteratorT> class Range {
  IteratorT first_, last_;

public:
  using iterator = IteratorT;
  Range(const IteratorT &first, const IteratorT &last)
      : first_(first), last_(last) {}
  iterator begin() { return first_; }
  iterator end() { return last_; }
};

/// Use \ref node_list_arg to construct list Nodes.
/// \ingroup utility
struct NodeListArg {
  explicit NodeListArg() = default;
};
constexpr NodeListArg node_list_arg{};

/// Use \ref node_map_arg to construct map Nodes.
/// \ingroup utility
struct NodeMapArg {
  explicit NodeMapArg() = default;
};
constexpr NodeMapArg node_map_arg{};

/// Use \ref utf8_string_arg to construct text string Nodes.
/// \ingroup utility
struct UTF8StringArg {
  explicit UTF8StringArg() = default;
};
constexpr UTF8StringArg utf8_string_arg{};

/// Use \ref byte_string_arg to construct byte string Nodes.
/// \ingroup utility
struct ByteStringArg {
  explicit ByteStringArg() = default;
};
constexpr ByteStringArg byte_string_arg{};

/// When specialized for a type, allows that type to be used with Node::as,
/// Node::is, etc.
/// \ingroup core
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

/// The essential kinds of data that can be stored by a Node.
/// https://github.com/ipld/specs/blob/master/data-model-layer/data-model.md
/// \ingroup core
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

/// A structured data value.
///
/// The possible values correspond to the IPLD Data Model:
/// https://github.com/ipld/specs/blob/master/data-model-layer/data-model.md
///
/// The API is based on jsoncons::basic_json:
/// https://github.com/danielaparker/jsoncons/blob/master/include/jsoncons/basic_json.hpp
/// Note that some terminology is different (jsoncons "object" is our "Map",
/// jsoncons "array" is our "List").
///
/// \ingroup core
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
  std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double,
               BytesStorage, StringStorage, CID, List, Map>
      variant_;

  void validateUTF8() const;
  void validateKeysUTF8() const;

  friend struct NodeTypeTraits<bool>;
  friend struct NodeTypeTraits<std::int64_t>;
  friend struct NodeTypeTraits<std::uint64_t>;
  friend struct NodeTypeTraits<double>;
  friend struct NodeTypeTraits<BytesRef>;
  friend struct NodeTypeTraits<llvm::StringRef>;
  friend struct NodeTypeTraits<CID>;

public:
  Node(const Node &Other) = default;
  Node(Node &&Other) noexcept = default;
  Node &operator=(const Node &Other) = default;
  Node &operator=(Node &&Other) noexcept = default;
  ~Node() noexcept {}

  /// \name Scalar constructors and assignment
  /// @{

  /// Construct a null Node. (NOTE: jsoncons default is empty map.)
  Node();

  /// Construct a null Node.
  Node(std::nullptr_t);

  /// Replace with null.
  Node &operator=(std::nullptr_t);

  /// Construct a boolean Node.
  Node(bool val);

#ifdef __DOXYGEN__
  /// Construct an integer Node.
  template <typename IntegerType> Node(IntegerType val);
#else // __DOXYGEN__

  // Doxygen gets confused by the '<' characters in template parameters.

  template <typename IntegerType,
            std::enable_if_t<std::is_integral<IntegerType>::value &&
                                 std::is_unsigned<IntegerType>::value &&
                                 sizeof(IntegerType) <= sizeof(std::uint64_t),
                             int> = 0>
  Node(IntegerType val) : variant_(std::uint64_t(val)) {}

  template <typename IntegerType,
            std::enable_if_t<std::is_integral<IntegerType>::value &&
                                 std::is_signed<IntegerType>::value &&
                                 sizeof(IntegerType) <= sizeof(std::int64_t),
                             int> = 0>
  Node(IntegerType val) : variant_(std::int64_t(val)) {}

#endif // __DOXYGEN__

  /// Construct a floating-point Node.
  Node(double Float);

  /// Construct a text string Node. The text must be valid UTF-8.
  Node(const char *Str);

  /// Construct a text string Node. The text must be valid UTF-8.
  Node(const char *Str, std::size_t Length);

  /// Replace with a text string. The text must be valid UTF-8.
  Node &operator=(const char *Str);

  /// Construct a text string Node. We require an explicit \ref utf8_string_arg
  /// argument to remind you that strings must be valid UTF-8.
  Node(UTF8StringArg, const llvm::StringRef &sr);

  /// Construct a text string Node. We require an explicit \ref utf8_string_arg
  /// argument to remind you that strings must be valid UTF-8.
  Node(UTF8StringArg, const std::string_view &sv);

  /// Construct a text string Node. We require an explicit \ref utf8_string_arg
  /// argument to remind you that strings must be valid UTF-8.
  Node(UTF8StringArg, const std::string &str);

  /// Construct an empty byte string Node.
  Node(ByteStringArg);

  /// Construct a byte string Node.
  // FIXME: Check whether Source really is a byte/char sequence.
  template <class Source>
  Node(ByteStringArg, const Source &source)
      : variant_(
            BytesStorage(reinterpret_cast<const std::uint8_t *>(source.data()),
                         reinterpret_cast<const std::uint8_t *>(source.data()) +
                             source.size())) {}

  /// Construct a byte string Node.
  Node(BytesRef bytes);

  // Explicit so we don't confuse Node values with links when setting up e.g.
  // call arguments.
  /// Construct a link Node.
  explicit Node(const CID &val);
  /// Construct a link Node.
  explicit Node(CID &&val);

  /// @}

  /// \name Container constructors and assignment
  /// @{

  /// Construct an empty list Node. The argument must be \ref node_list_arg.
  explicit Node(NodeListArg);

  /// Construct a list Node. The first argument must be \ref node_list_arg.
  template <class InputIt>
  Node(NodeListArg, InputIt first, InputIt last) : Node(List(first, last)) {}

  /// Construct a list Node. The first argument must be \ref node_list_arg.
  /// Example:
  /// \code
  ///   Node(node_list_arg, {0, 1, 2})
  /// \endcode
  Node(NodeListArg, std::initializer_list<Node> init);

  Node(const List &list);
  Node(List &&list);

  /// Construct an empty map Node. The argument must be \ref node_map_arg.
  explicit Node(NodeMapArg);

  /// Construct a map Node. The first argument must be \ref node_map_arg.
  template <class InputIt>
  Node(NodeMapArg, InputIt first, InputIt last) : Node(Map(first, last)) {
    validateKeysUTF8();
  }

  /// Construct a map Node. The first argument must be \ref node_map_arg.
  /// Example:
  /// \code
  ///   Node(node_map_arg, {{"number", 27}, {"string", "goodbyte"}})
  /// \endcode
  Node(NodeMapArg,
       std::initializer_list<std::pair<llvm::StringRef, Node>> init);

  Node(const Map &map);
  Node(Map &&map);

  /// @}

  /// \name Comparison operators
  /// Nodes of the same kind will be sorted in the natural way (integers in
  /// ascending order, strings in lexicographical order, etc.) Nodes of
  /// different kinds are sorted in an arbitrary order.
  /// @{

  bool operator==(const Node &other) const;
  bool operator!=(const Node &other) const;
  bool operator<(const Node &other) const;
  bool operator<=(const Node &other) const;
  bool operator>(const Node &other) const;
  bool operator>=(const Node &other) const;
  int compare(const Node &other) const;

  /// @}

  /// \name Saving and loading
  /// @{

  /// Load a Node from CBOR bytes:
  /// https://www.rfc-editor.org/rfc/rfc8949.html
  static llvm::Expected<Node> loadFromCBOR(BytesRef in);

  /// Load a Node from CBOR bytes at the beginning of a sequence. When this
  /// returns, "in" will refer to the rest of the bytes after the CBOR value.
  static llvm::Expected<Node> loadFromCBORSequence(BytesRef &in);

  struct CBORInfo {
    /// The encoded CBOR includes links (CIDs, tag 42).
    bool has_links = false;
    /// The encoded CBOR is not valid DAG-CBOR.
    bool not_dag_cbor = false;
  };

  /// Save a Node to CBOR bytes.
  void save_cbor(std::vector<std::uint8_t> &out,
                 CBORInfo *info = nullptr) const;

  /// Load a Node from a CID and the corresponding content bytes. The CID
  /// content type may be Raw (bytes returned as a bytestring Node), DAG-CBOR,
  /// or DAG-CBOR-Unrestricted. The CID hash type may be Identity, in which
  /// case the value is loaded directly from the CID.
  static llvm::Expected<Node> loadFromIPLD(const CID &CID, BytesRef Content);

  /// Save a Node as a CID and the corresponding content bytes. The CID content
  /// type will be either Raw (if this is a bytestring Node), DAG-CBOR, or
  /// DAG-CBOR-Unrestricted. If noIdentity is false and the value is small
  /// enough, the CID / will be an Identity CID and the returned content bytes
  /// will be empty.
  std::pair<CID, std::vector<std::uint8_t>>
  saveAsIPLD(bool noIdentity = false) const;

  /// Load a Node from the MemoDB JSON format.
  static llvm::Expected<Node> loadFromJSON(llvm::StringRef json);

  /// @}

  /// \name Checking the kind of Node
  /// @{

  Kind kind() const;

  constexpr bool is_null() const noexcept {
    return std::holds_alternative<std::monostate>(variant_);
  }

  constexpr bool is_boolean() const noexcept {
    return std::holds_alternative<bool>(variant_);
  }

  constexpr bool is_float() const noexcept {
    return std::holds_alternative<double>(variant_);
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

  /// @}

  /// \name Templated accessors
  /// These functions use NodeTypeTraits to support lots of different types.
  /// You can specialize NodeTypeTraits to make them work for your own types,
  /// too.
  /// @{

  /// Check whether this Node can be converted to type T.
  /// \code
  ///   node.is<int>()
  /// \endcode
  template <class T> constexpr bool is() const {
    return NodeTypeTraits<T>::is(*this);
  }

  /// Convert this Node to type T, aborting if this is impossible.
  /// \code
  ///   node.as<int>()
  /// \endcode
  template <class T> T as() const { return NodeTypeTraits<T>::as(*this); }

  /// Convert this Node to type T in bytestring mode, aborting if this is
  /// impossible.
  template <class T> T as(ByteStringArg) const {
    return NodeTypeTraits<T>::as(*this, byte_string_arg);
  }

  /// @}

  /// \name Access to text strings, byte strings, maps, and lists
  /// @{

  // NOTE: works on strings and bytes, unlike jsoncons.
  std::size_t size() const;

  bool empty() const noexcept;

  /// Works on lists and maps only, not strings!
  void clear();

  /// @}

  /// \name Access to lists
  /// @{

  Node &operator[](std::size_t i);
  const Node &operator[](std::size_t i) const;

  void resize(std::size_t n);

  template <class T> void resize(std::size_t n, T val) {
    if (List *value = std::get_if<List>(&variant_))
      value->resize(n, val);
  }

  // Stricter than jsoncons (maps don't work).
  Node &at(std::size_t i);

  // Stricter than jsoncons (maps don't work).
  const Node &at(std::size_t i) const;

  template <class... Args> Node &emplace_back(Args &&...args) {
    return std::get<List>(variant_).emplace_back(std::forward<Args>(args)...);
  }

  template <class T> void push_back(T &&val) {
    std::get<List>(variant_).push_back(std::forward<T>(val));
  }

  /// Returns a pair of iterators to iterate over the list contents.
  Range<List::iterator> list_range();

  /// Returns a pair of iterators to iterate over the list contents.
  Range<List::const_iterator> list_range() const;

  /// @}

  /// \name Access to maps
  /// Note that all map keys must be valid UTF-8 strings.
  /// @{

  Node &operator[](const llvm::StringRef &name);
  const Node &operator[](const llvm::StringRef &name) const;

  bool contains(const llvm::StringRef &key) const noexcept;

  std::size_t count(const llvm::StringRef &key) const;

  Node &at(const llvm::StringRef &name);
  const Node &at(const llvm::StringRef &name) const;

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
                               : iter->value().as<T>();
  }

  void erase(const llvm::StringRef &name);

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

  /// Returns a pair of iterators to iterate over the map contents.
  Range<Map::iterator> map_range();

  /// Returns a pair of iterators to iterate over the map contents.
  Range<Map::const_iterator> map_range() const;

  /// @}

  /// \name Extra functions
  /// @{

  /// Traverse this Node and call func for each CID found.
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

  /// @}
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
    return std::visit(Overloaded{
                          [](const std::int64_t &) { return true; },
                          [](const std::uint64_t &value) {
                            return value <=
                                   std::numeric_limits<std::int64_t>::max();
                          },
                          [](const auto &) { return false; },
                      },
                      node.variant_);
  }

  static std::int64_t as(const Node &node) {
    if (!is(node))
      llvm::report_fatal_error("Integer overflow or not an integer");
    return std::visit(Overloaded{
                          [](const std::int64_t &value) { return value; },
                          [](const std::uint64_t &value) {
                            return static_cast<std::int64_t>(value);
                          },
                          [](const auto &) -> std::int64_t {
                            llvm_unreachable("impossible");
                          },
                      },
                      node.variant_);
  }
};

template <> struct NodeTypeTraits<std::uint64_t> {
  static constexpr bool is(const Node &node) noexcept {
    return std::visit(Overloaded{
                          [](const std::int64_t &value) { return value >= 0; },
                          [](const std::uint64_t &value) { return true; },
                          [](const auto &) { return false; },
                      },
                      node.variant_);
  }

  static std::int64_t as(const Node &node) {
    if (!is(node))
      llvm::report_fatal_error("Integer overflow or not an integer");
    return std::visit(Overloaded{
                          [](const std::int64_t &value) {
                            return static_cast<std::uint64_t>(value);
                          },
                          [](const std::uint64_t &value) { return value; },
                          [](const auto &) -> std::uint64_t {
                            llvm_unreachable("impossible");
                          },
                      },
                      node.variant_);
  }
};

template <class IntegerType>
struct NodeTypeTraits<
    IntegerType,
    typename std::enable_if<std::is_integral<IntegerType>::value &&
                            std::is_unsigned<IntegerType>::value>::type> {
  static bool is(const Node &node) noexcept {
    if (!node.is<std::uint64_t>())
      return false;
    const std::uint64_t value = node.as<std::uint64_t>();
    return static_cast<std::uint64_t>(static_cast<IntegerType>(value)) == value;
  }

  static IntegerType as(const Node &node) {
    if (!is(node))
      llvm::report_fatal_error("Integer overflow or not an integer");
    return static_cast<IntegerType>(node.as<std::uint64_t>());
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
    return std::visit(Overloaded{
                          [](const double &) { return true; },
                          [](const std::int64_t &) { return true; },
                          [](const std::uint64_t &) { return true; },
                          [](const auto &) { return false; },
                      },
                      node.variant_);
  }

  static double as(const Node &node) {
    return std::visit(Overloaded{
                          [](const double &value) { return value; },
                          [](const std::int64_t &value) {
                            return static_cast<double>(value);
                          },
                          [](const std::uint64_t &value) {
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
