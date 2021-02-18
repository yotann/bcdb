#ifndef MEMODB_MEMODB_H
#define MEMODB_MEMODB_H

#include <iosfwd>
#include <map>
#include <memory>
#include <stddef.h>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>

namespace llvm {
class raw_ostream;
} // end namespace llvm

// https://en.cppreference.com/w/cpp/utility/variant/visit
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

class memodb_ref {
private:
  std::string id_;

public:
  memodb_ref() : id_() {}
  memodb_ref(llvm::StringRef value) : id_(value) {}
  operator llvm::StringRef() const { return id_; }
  operator bool() const { return !id_.empty(); }
  bool operator<(const memodb_ref &other) const { return id_ < other.id_; }
  bool operator==(const memodb_ref &other) const { return id_ == other.id_; }
};

class memodb_value {
public:
  enum value_t {
    UNDEFINED,
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

  using value_type = memodb_value;
  using reference = value_type &;
  using const_reference = const value_type &;
  using difference_type = std::ptrdiff_t;
  using size_type = std::size_t;
  using pointer = value_type *;
  using const_pointer = const value_type *;
  // TODO: iterators

  struct undefined_t : std::monostate {};
  struct null_t : std::monostate {};
  using bool_t = bool;
  using integer_t = std::int64_t;
  using float_t = double;
  using bytes_t = std::vector<std::uint8_t>;
  using string_t = std::string;
  using ref_t = memodb_ref;
  using array_t = std::vector<memodb_value>;
  using map_t = std::map<memodb_value, memodb_value>;

private:
  using variant_type =
      std::variant<undefined_t, null_t, bool_t, integer_t, float_t, bytes_t,
                   string_t, ref_t, array_t, map_t>;

  variant_type variant_;

  template <typename T> T &as() {
    T *x = std::get_if<T>(&variant_);
    if (!x)
      llvm::report_fatal_error("invalid memodb_value type");
    return *x;
  }

  template <typename T> const T &as() const {
    return const_cast<memodb_value *>(this)->as<T>();
  }

public:
  memodb_value() : variant_() {}
  memodb_value(std::nullptr_t) : variant_(null_t()) {}

  memodb_value(undefined_t val) : variant_(val) {}
  memodb_value(null_t val) : variant_(val) {}
  memodb_value(bool_t val) : variant_(val) {}
  memodb_value(integer_t val) : variant_(integer_t(val)) {}
  memodb_value(float_t val) : variant_(val) {}

  memodb_value(const bytes_t &val) : variant_(val) {}
  memodb_value(const string_t &val) : variant_(val) {}
  memodb_value(const ref_t &val) : variant_(val) {}
  memodb_value(const array_t &val) : variant_(val) {}
  memodb_value(const map_t &val) : variant_(val) {}

  memodb_value(bytes_t &&val) : variant_(std::move(val)) {}
  memodb_value(string_t &&val) : variant_(std::move(val)) {}
  memodb_value(ref_t &&val) : variant_(std::move(val)) {}
  memodb_value(array_t &&val) : variant_(std::move(val)) {}
  memodb_value(map_t &&val) : variant_(std::move(val)) {}

  template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
  memodb_value(T val) : variant_(integer_t(val)) {}
  template <typename T,
            std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
  memodb_value(T val) : variant_(float_t(val)) {}

  memodb_value(llvm::ArrayRef<std::uint8_t> val) : variant_(bytes_t(val)) {}
  static memodb_value bytes(llvm::StringRef val) {
    return memodb_value(
        llvm::ArrayRef<unsigned char>(val.bytes_begin(), val.bytes_end()));
  }

  // TODO: verify that value is valid UTF-8.
  memodb_value(const char *val) : variant_(string_t(val)) {}
  // Explicit because strings are often not valid UTF-8.
  static memodb_value string(llvm::StringRef val) {
    memodb_value result;
    result.variant_ = string_t(val);
    return result;
  }

  static memodb_value
  array(std::initializer_list<array_t::value_type> init = {}) {
    memodb_value result;
    result.variant_ = array_t(init);
    return result;
  }
  template <class InputIT>
  static memodb_value array(InputIT first, InputIT last);

  static memodb_value map(std::initializer_list<map_t::value_type> init = {}) {
    memodb_value result;
    result.variant_ = map_t(init);
    return result;
  }
  template <class InputIT> static memodb_value map(InputIT first, InputIT last);

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
                          [](const undefined_t &) { return UNDEFINED; },
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

  static memodb_value load_cbor(llvm::ArrayRef<std::uint8_t> in) {
    return load_cbor_ref(in);
  }
  void save_cbor(std::vector<std::uint8_t> &out) const;

private:
  static memodb_value load_cbor_ref(llvm::ArrayRef<std::uint8_t> &in);

  void require_type(value_t type) const {
    if (type != this->type()) {
      llvm::report_fatal_error("invalid memodb_value type");
    }
  }

public:
  reference at(const value_type &idx) {
    if (type() == ARRAY) {
      return array_items().at(idx.as_integer());
    } else {
      return map_items().at(idx);
    }
  }

  const_reference at(const value_type &idx) const {
    if (type() == ARRAY) {
      return array_items().at(idx.as_integer());
    } else {
      return map_items().at(idx);
    }
  }

  reference operator[](const value_type &idx) {
    if (type() == ARRAY) {
      return at(idx);
    } else {
      return map_items()[idx];
    }
  }

  const_reference operator[](const value_type &idx) const {
    if (type() == ARRAY) {
      return at(idx);
    } else {
      const map_t &map = map_items();
      const auto iter = map.find(idx);
      assert(iter != map.end());
      return iter->second;
    }
  }

  bool operator<(const memodb_value &other) const {
    return variant_ < other.variant_;
  }

  bool operator==(const memodb_value &other) const {
    return variant_ == other.variant_;
  }

  bool operator!=(const memodb_value &other) const {
    return variant_ != other.variant_;
  }

  friend std::ostream &operator<<(std::ostream &, const memodb_value &);
};

std::ostream &operator<<(std::ostream &os, const memodb_value &value);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_value &value);

using memodb_path = std::vector<memodb_value>;

class memodb_db {
public:
  virtual ~memodb_db() {}

  virtual memodb_value get(const memodb_ref &ref) = 0;
  virtual memodb_ref put(const memodb_value &value) = 0;
  virtual std::vector<memodb_ref> list_refs_using(const memodb_ref &ref) = 0;

  virtual std::vector<std::string> list_heads() = 0;
  virtual std::vector<std::string> list_heads_using(const memodb_ref &ref) = 0;
  virtual memodb_ref head_get(llvm::StringRef name) = 0;
  virtual void head_set(llvm::StringRef name, const memodb_ref &ref) = 0;
  virtual void head_delete(llvm::StringRef name) = 0;

  virtual memodb_ref call_get(llvm::StringRef name,
                              llvm::ArrayRef<memodb_ref> args) = 0;
  virtual void call_set(llvm::StringRef name, llvm::ArrayRef<memodb_ref> args,
                        const memodb_ref &result) = 0;
  virtual void call_invalidate(llvm::StringRef name) = 0;

  virtual std::vector<memodb_path> list_paths_to(const memodb_ref &ref);
};

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing = false);

#endif // MEMODB_MEMODB_H
