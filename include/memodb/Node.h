#ifndef MEMODB_NODE_H
#define MEMODB_NODE_H

#include <cstddef>
#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <map>
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

class Node {
public:
  enum value_t {
    NULL_TYPE,
    BOOL,
    INTEGER,
    FLOAT,
    BYTES,
    STRING,
    REF,
    ARRAY,
    MAP,
  };

  using value_type = Node;
  using reference = value_type &;
  using const_reference = const value_type &;
  using difference_type = std::ptrdiff_t;
  using size_type = std::size_t;
  using pointer = value_type *;
  using const_pointer = const value_type *;
  // TODO: iterators

  struct null_t : std::monostate {};
  using bool_t = bool;
  using integer_t = std::int64_t;
  using float_t = double;
  using bytes_t = std::vector<std::uint8_t>;
  using string_t = std::string;
  using ref_t = CID;
  using array_t = std::vector<Node>;
  using map_t = std::map<string_t, Node>;

private:
  using variant_type = std::variant<null_t, bool_t, integer_t, float_t, bytes_t,
                                    string_t, ref_t, array_t, map_t>;

  variant_type variant_;

  template <typename T> T &as() {
    T *x = std::get_if<T>(&variant_);
    if (!x)
      llvm::report_fatal_error("invalid Node type");
    return *x;
  }

  template <typename T> const T &as() const {
    return const_cast<Node *>(this)->as<T>();
  }

  void validateUTF8() const;

public:
  Node() : variant_() {}
  Node(std::nullptr_t) : variant_(null_t()) {}

  Node(null_t val) : variant_(val) {}
  Node(bool_t val) : variant_(val) {}
  Node(integer_t val) : variant_(integer_t(val)) {}
  Node(float_t val) : variant_(val) {}

  Node(const bytes_t &val) : variant_(val) {}
  Node(const string_t &val) : variant_(val) { validateUTF8(); }
  Node(const ref_t &val) : variant_(val) {}
  Node(const array_t &val) : variant_(val) {}
  Node(const map_t &val) : variant_(val) {}

  Node(bytes_t &&val) : variant_(std::move(val)) {}
  Node(string_t &&val) : variant_(std::move(val)) { validateUTF8(); }
  Node(ref_t &&val) : variant_(std::move(val)) {}
  Node(array_t &&val) : variant_(std::move(val)) {}
  Node(map_t &&val) : variant_(std::move(val)) {}

  template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
  Node(T val) : variant_(integer_t(val)) {}
  template <typename T,
            std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
  Node(T val) : variant_(float_t(val)) {}

  Node(llvm::ArrayRef<std::uint8_t> val) : variant_(bytes_t(val)) {}
  static Node bytes() { return Node(bytes_t{}); }
  static Node bytes(llvm::ArrayRef<char> val) {
    return Node(llvm::ArrayRef<unsigned char>(
        reinterpret_cast<const unsigned char *>(val.begin()),
        reinterpret_cast<const unsigned char *>(val.end())));
  }
  static Node bytes(llvm::StringRef val) {
    return Node(
        llvm::ArrayRef<unsigned char>(val.bytes_begin(), val.bytes_end()));
  }

  Node(const char *val) : variant_(string_t(val)) {}

  // Explicit because strings are often not valid UTF-8.
  static Node string(llvm::StringRef val) {
    Node result;
    result.variant_ = string_t(val);
    result.validateUTF8();
    return result;
  }

  static Node array(std::initializer_list<array_t::value_type> init = {}) {
    Node result;
    result.variant_ = array_t(init);
    return result;
  }
  template <class InputIT> static Node array(InputIT first, InputIT last);

  static Node map(std::initializer_list<map_t::value_type> init = {}) {
    Node result;
    result.variant_ = map_t(init);
    return result;
  }
  template <class InputIT> static Node map(InputIT first, InputIT last);

  bool_t as_bool() const { return as<bool_t>(); }
  integer_t as_integer() const { return as<integer_t>(); }
  float_t as_float() const { return as<float_t>(); }
  const ref_t &as_ref() const { return as<ref_t>(); }
  const string_t &as_string() const { return as<string_t>(); }
  const llvm::ArrayRef<std::uint8_t> as_bytes() const { return as<bytes_t>(); }
  const llvm::StringRef as_bytestring() const {
    const bytes_t &bytes = as<bytes_t>();
    return llvm::StringRef(reinterpret_cast<const char *>(bytes.data()),
                           bytes.size());
  }

  const array_t &array_items() const { return as<array_t>(); }

  array_t &array_items() { return as<array_t>(); }

  const map_t &map_items() const { return as<map_t>(); }

  map_t &map_items() { return as<map_t>(); }

  constexpr value_t type() const noexcept {
    return std::visit(overloaded{
                          [](const null_t &) { return NULL_TYPE; },
                          [](const bool_t &) { return BOOL; },
                          [](const integer_t &) { return INTEGER; },
                          [](const float_t &) { return FLOAT; },
                          [](const bytes_t &) { return BYTES; },
                          [](const string_t &) { return STRING; },
                          [](const ref_t &) { return REF; },
                          [](const array_t &) { return ARRAY; },
                          [](const map_t &) { return MAP; },
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
  std::pair<CID, bytes_t> saveAsIPLD(bool noIdentity = false) const;

private:
  void require_type(value_t type) const {
    if (type != this->type()) {
      llvm::report_fatal_error("invalid Node type");
    }
  }

public:
  reference at(integer_t idx) { return array_items().at(idx); }
  const_reference at(integer_t idx) const { return array_items().at(idx); }
  reference operator[](integer_t idx) { return at(idx); }
  const_reference operator[](integer_t idx) const { return at(idx); }

  reference at(const string_t &idx) { return map_items().at(idx); }
  const_reference at(const string_t &idx) const { return map_items().at(idx); }
  reference operator[](const string_t &idx) { return map_items()[idx]; }
  const_reference operator[](const string_t &idx) const {
    const map_t &map = map_items();
    const auto iter = map.find(idx);
    assert(iter != map.end());
    return iter->second;
  }

  bool operator<(const Node &other) const { return variant_ < other.variant_; }

  bool operator==(const Node &other) const {
    return variant_ == other.variant_;
  }

  bool operator!=(const Node &other) const {
    return variant_ != other.variant_;
  }

  friend std::ostream &operator<<(std::ostream &, const Node &);
};

std::ostream &operator<<(std::ostream &os, const Node &value);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Node &value);

} // end namespace memodb

#endif // MEMODB_NODE_H
