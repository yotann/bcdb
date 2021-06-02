#ifndef MEMODB_NODE_H
#define MEMODB_NODE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <variant>
#include <vector>

#include "CID.h"

namespace memodb {

// https://en.cppreference.com/w/cpp/utility/variant/visit
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// LLVM symbol names are usually ASCII, but can contain arbitrary bytes. We
// interpret the bytes as ISO-8859-1 and convert them to UTF-8 for use in map
// keys.
std::string bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes);
std::string bytesToUTF8(llvm::StringRef Bytes);
std::string utf8ToByteString(llvm::StringRef Str);

class Node;

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

template <class KeyT, class ValueT> class NodeMap {
public:
  using key_value_type = KeyValue<KeyT, ValueT>;

private:
  using container_type = std::vector<key_value_type>;
  container_type members_;
  static bool compare(const key_value_type &a, const llvm::StringRef &b) {
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

template <class IteratorT> class range {
  IteratorT first_, last_;

public:
  using iterator = IteratorT;
  range(const IteratorT &first, const IteratorT &last)
      : first_(first), last_(last) {}
  iterator begin() { return first_; }
  iterator end() { return last_; }
};

struct node_list_arg_t {
  explicit node_list_arg_t() = default;
};
constexpr node_list_arg_t node_list_arg{};

struct node_map_arg_t {
  explicit node_map_arg_t() = default;
};
constexpr node_map_arg_t node_map_arg{};

struct utf8_string_arg_t {
  explicit utf8_string_arg_t() = default;
};
constexpr utf8_string_arg_t utf8_string_arg{};

struct byte_string_arg_t {
  explicit byte_string_arg_t() = default;
};
constexpr byte_string_arg_t byte_string_arg{};

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
// Note that some terminology is different (jsoncons "object" is our "map",
// jsoncons "array" is our "list").
class Node {

public:
  using reference = Node &;
  using const_reference = const Node &;
  using pointer = Node *;
  using const_pointer = const Node *;

  using key_type = std::string;
  using key_value_type = std::pair<key_type, Node>;
  using list = std::vector<Node>;
  using map = NodeMap<llvm::SmallString<12>, Node>;

  using map_iterator = typename map::iterator;
  using const_map_iterator = typename map::const_iterator;
  using list_iterator = typename list::iterator;
  using const_list_iterator = typename list::const_iterator;

private:
  using bytes_storage = llvm::SmallVector<std::uint8_t, 48>;
  using string_storage = llvm::SmallString<48>;
  using variant_type =
      std::variant<std::monostate, bool, std::int64_t, double, bytes_storage,
                   string_storage, CID, list, map>;

  variant_type variant_;

  void validateUTF8() const;
  void validateKeysUTF8() const;

public:
  Node &operator=(const Node &Other) = default;
  Node &operator=(Node &&Other) noexcept = default;

  Kind kind() const {
    return std::visit(overloaded{
                          [](const std::monostate &) { return Kind::Null; },
                          [](const bool &) { return Kind::Boolean; },
                          [](const std::int64_t &) { return Kind::Integer; },
                          [](const double &) { return Kind::Float; },
                          [](const string_storage &) { return Kind::String; },
                          [](const bytes_storage &) { return Kind::Bytes; },
                          [](const list &) { return Kind::List; },
                          [](const map &) { return Kind::Map; },
                          [](const CID &) { return Kind::Link; },
                      },
                      variant_);
  }

  // NOTE: works on strings and bytes, unlike jsoncons.
  std::size_t size() const {
    return std::visit(
        overloaded{
            [](const string_storage &val) -> std::size_t { return val.size(); },
            [](const bytes_storage &val) -> std::size_t { return val.size(); },
            [](const list &List) -> std::size_t { return List.size(); },
            [](const map &Map) -> std::size_t { return Map.size(); },
            [](const auto &X) -> std::size_t { return 0; },
        },
        variant_);
  }

  llvm::StringRef as_string_ref() const {
    return std::get<string_storage>(variant_);
  }

  // NOTE: default is null (jsoncons default is empty map).
  Node() : variant_() {}
  Node(const Node &Other) = default;
  Node(Node &&Other) noexcept = default;

  explicit Node(node_map_arg_t) : Node(map()) {}

  template <class InputIt>
  Node(node_map_arg_t, InputIt first, InputIt last) : Node(map(first, last)) {
    validateKeysUTF8();
  }

  Node(node_map_arg_t,
       std::initializer_list<std::pair<llvm::StringRef, Node>> init)
      : Node(map(init)) {
    validateKeysUTF8();
  }

  explicit Node(node_list_arg_t) : Node(list()) {}

  template <class InputIt>
  Node(node_list_arg_t, InputIt first, InputIt last)
      : Node(list(first, last)) {}

  Node(node_list_arg_t, std::initializer_list<Node> init) : Node(list(init)) {}

  Node(const list &List) : variant_(List) {}
  Node(list &&List) : variant_(std::forward<list>(List)) {}
  Node(const map &Map) : variant_(Map) { validateKeysUTF8(); }
  Node(map &&Map) : variant_(std::forward<map>(Map)) { validateKeysUTF8(); }

  Node(const char *Str) : variant_(string_storage(Str)) { validateUTF8(); }
  Node(const char *Str, std::size_t Length)
      : variant_(string_storage(Str, Str + Length)) {
    validateUTF8();
  }

  Node(double Float) : variant_(Float) {}

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

  // We require an explicit utf8_string_arg argument because strings are often
  // not valid UTF-8.
  Node(utf8_string_arg_t, const llvm::StringRef &sr)
      : variant_(string_storage(sr)) {
    validateUTF8();
  }
  Node(utf8_string_arg_t, const std::string_view &sv)
      : variant_(string_storage(sv.begin(), sv.end())) {
    validateUTF8();
  }
  Node(utf8_string_arg_t, const std::string &str)
      : variant_(string_storage(str)) {
    validateUTF8();
  }

  Node(std::nullptr_t) : variant_() {}
  Node(bool val) : variant_(val) {}

  Node(byte_string_arg_t) : variant_(bytes_storage()) {}

  // FIXME: check whether it's actually a byte/char sequence
  template <class Source>
  Node(byte_string_arg_t, const Source &source)
      : variant_(bytes_storage(
            reinterpret_cast<const std::uint8_t *>(source.data()),
            reinterpret_cast<const std::uint8_t *>(source.data()) +
                source.size())) {}

  Node(llvm::ArrayRef<std::uint8_t> bytes) : Node(byte_string_arg, bytes) {}

  Node(const CID &val) : variant_(val) {}
  Node(CID &&val) : variant_(std::forward<CID>(val)) {}

  ~Node() noexcept {}

  Node &operator=(const char *Str) { return *this = Node(Str); }

  Node &operator[](std::size_t i) { return at(i); }

  const Node &operator[](std::size_t i) const { return at(i); }

  Node &operator[](const llvm::StringRef &name) {
    return std::get<map>(variant_).try_emplace(name).first->value();
  }

  const Node &operator[](const llvm::StringRef &name) const { return at(name); }

  bool is_null() const noexcept {
    return std::holds_alternative<std::monostate>(variant_);
  }

  bool contains(const llvm::StringRef &key) const noexcept {
    return count(key) != 0;
  }

  std::size_t count(const llvm::StringRef &key) const {
    if (const map *value = std::get_if<map>(&variant_))
      return value->find(std::string(key)) != value->end() ? 1 : 0;
    return 0;
  }

  bool is_string() const noexcept {
    return std::holds_alternative<string_storage>(variant_);
  }

  bool is_string_view() const noexcept { return is_string(); }

  bool is_bytes() const noexcept {
    return std::holds_alternative<bytes_storage>(variant_);
  }

  bool is_bytes_view() const noexcept { return is_bytes(); }

  bool is_boolean() const noexcept {
    return std::holds_alternative<bool>(variant_);
  }

  bool is_map() const noexcept { return std::holds_alternative<map>(variant_); }

  bool is_list() const noexcept {
    return std::holds_alternative<list>(variant_);
  }

  bool is_integer() const noexcept {
    return std::holds_alternative<std::int64_t>(variant_);
  }

  bool is_float() const noexcept {
    return std::holds_alternative<double>(variant_);
  }

  bool is_number() const noexcept { return is_integer() || is_float(); }

  bool empty() const noexcept {
    return std::visit(overloaded{
                          [](const string_storage &str) { return str.empty(); },
                          [](const std::vector<std::uint8_t> &bytes) {
                            return bytes.empty();
                          },
                          [](const list &list) { return list.empty(); },
                          [](const map &map) { return map.empty(); },
                          [](const auto &) { return false; },
                      },
                      variant_);
  }

  void resize(std::size_t n) {
    if (list *value = std::get_if<list>(&variant_))
      value->resize(n);
  }

  template <class T> void resize(std::size_t n, T val) {
    if (list *value = std::get_if<list>(&variant_))
      value->resize(n, val);
  }

  template <class T> T as(byte_string_arg_t) const {
    const auto &bytes = std::get<bytes_storage>(variant_);
    return T(bytes.begin(), bytes.end());
  }

  template <> llvm::StringRef as<llvm::StringRef>(byte_string_arg_t) const {
    const auto &bytes = std::get<bytes_storage>(variant_);
    return llvm::StringRef(reinterpret_cast<const char *>(bytes.data()),
                           bytes.size());
  }

  // Stricter than jsoncons (integers don't work).
  bool as_boolean() const { return std::get<bool>(variant_); }

  // Stricter than jsoncons (floats and booleans don't work).
  template <class IntegerType> IntegerType as_integer() const {
    if (!is_integer<IntegerType>())
      llvm::report_fatal_error("Integer overflow or not an integer");
    return static_cast<IntegerType>(std::get<std::int64_t>(variant_));
  }

  template <class IntegerType,
            std::enable_if_t<std::is_integral<IntegerType>::value &&
                                 std::is_signed<IntegerType>::value,
                             int> = 0>
  bool is_integer() const noexcept {
    if (const std::int64_t *value = std::get_if<std::int64_t>(&variant_))
      return *value >= std::numeric_limits<IntegerType>::lowest() &&
             *value <= std::numeric_limits<IntegerType>::max();
    return false;
  }

  template <class IntegerType,
            std::enable_if_t<std::is_integral<IntegerType>::value &&
                                 std::is_unsigned<IntegerType>::value,
                             int> = 0>
  bool is_integer() const noexcept {
    if (const std::int64_t *value = std::get_if<std::int64_t>(&variant_))
      return *value >= 0 && static_cast<std::int64_t>(
                                static_cast<IntegerType>(*value)) == *value;
    return false;
  }

  double as_float() const {
    return std::visit(
        overloaded{
            [](const double &value) { return value; },
            [](const std::int64_t &value) { return double(value); },
            [](const auto &) {
              llvm::report_fatal_error("Not a number");
              return 0.0;
            },
        },
        variant_);
  }

  // Stricter than jsoncons (bytes and arrays don't work).
  std::string as_string() const {
    return std::string(std::get<string_storage>(variant_));
  }

  const CID &as_link() const { return std::get<CID>(variant_); }

  Node &at(const llvm::StringRef &name) {
    auto &value = std::get<map>(variant_);
    auto iter = value.find(name);
    if (iter == value.end())
      llvm::report_fatal_error("Key \"" + std::string(name) + "\" not found");
    return iter->value();
  }

  const Node &at(const llvm::StringRef &name) const {
    const auto &value = std::get<map>(variant_);
    auto iter = value.find(name);
    if (iter == value.end())
      llvm::report_fatal_error("Key \"" + std::string(name) + "\" not found");
    return iter->value();
  }

  // Stricter than jsoncons (maps don't work).
  Node &at(std::size_t i) { return std::get<list>(variant_).at(i); }

  // Stricter than jsoncons (maps don't work).
  const Node &at(std::size_t i) const { return std::get<list>(variant_).at(i); }

  map_iterator find(const llvm::StringRef &name) {
    return std::get<map>(variant_).find(name);
  }

  const_map_iterator find(const llvm::StringRef &name) const {
    return std::get<map>(variant_).find(name);
  }

  const Node &at_or_null(const llvm::StringRef &name) const {
    static const Node null_node = nullptr;
    const map &value = std::get<map>(variant_);
    auto iter = value.find(name);
    return iter == value.end() ? null_node : iter->value();
  }

  void clear() {
    std::visit(overloaded{
                   [](list &val) { val.clear(); },
                   [](map &val) { val.clear(); },
                   [](auto &) {},
               },
               variant_);
  }

  template <class T>
  std::pair<map_iterator, bool> insert_or_assign(const llvm::StringRef &name,
                                                 T &&val) {
    return std::get<map>(variant_).insert_or_assign(name, std::forward<T>(val));
  }

  template <class... Args>
  std::pair<map_iterator, bool> try_emplace(const llvm::StringRef &name,
                                            Args &&...args) {
    return std::get<map>(variant_).try_emplace(name,
                                               std::forward<Args>(args)...);
  }

  template <class... Args> Node &emplace_back(Args &&...args) {
    return std::get<list>(variant_).emplace_back(std::forward<Args>(args)...);
  }

  template <class T> void push_back(T &&val) {
    std::get<list>(variant_).push_back(std::forward<T>(val));
  }

  range<map_iterator> map_range() {
    auto &value = std::get<map>(variant_);
    return range(value.begin(), value.end());
  }

  range<const_map_iterator> map_range() const {
    const auto &value = std::get<map>(variant_);
    return range(value.begin(), value.end());
  }

  range<list_iterator> list_range() {
    auto &value = std::get<list>(variant_);
    return range(value.begin(), value.end());
  }

  range<const_list_iterator> list_range() const {
    const auto &value = std::get<list>(variant_);
    return range(value.begin(), value.end());
  }

public:
  template <typename T> void eachLink(T Func) const {
    std::visit(overloaded{
                   [&](const CID &Link) { Func(Link); },
                   [&](const list &List) {
                     for (const auto &Item : List)
                       Item.eachLink(Func);
                   },
                   [&](const map &Map) {
                     for (const auto &Item : Map)
                       Item.value().eachLink(Func);
                   },
                   [&](const auto &X) {},
               },
               variant_);
  }

  static Node load_cbor(llvm::ArrayRef<std::uint8_t> in) {
    return load_cbor_from_sequence(in);
  }
  static Node load_cbor_from_sequence(llvm::ArrayRef<std::uint8_t> &in);
  static Node loadFromIPLD(const CID &CID,
                           llvm::ArrayRef<std::uint8_t> Content);
  void save_cbor(std::vector<std::uint8_t> &out) const;
  std::pair<CID, std::vector<std::uint8_t>>
  saveAsIPLD(bool noIdentity = false) const;

  bool operator==(const Node &other) const {
    return variant_ == other.variant_;
  }

  bool operator!=(const Node &other) const {
    return variant_ != other.variant_;
  }

  bool operator<(const Node &other) const { return variant_ < other.variant_; }

  friend std::ostream &operator<<(std::ostream &, const Node &);
};

std::ostream &operator<<(std::ostream &os, const Node &value);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Node &value);

} // end namespace memodb

#endif // MEMODB_NODE_H
