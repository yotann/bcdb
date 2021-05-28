#ifndef MEMODB_MEMODB_H
#define MEMODB_MEMODB_H

#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <stddef.h>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

namespace llvm {
class raw_ostream;
} // end namespace llvm

// https://en.cppreference.com/w/cpp/utility/variant/visit
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

struct ParsedURI {
public:
  ParsedURI(llvm::StringRef URI);

  // If input is "x:/foo%2Fbar", Path will be "/foo/bar" and PathSegments will
  // be ["", "foo%2Fbar"].
  std::string Scheme, Authority, Path, Query, Fragment;
  std::vector<std::string> PathSegments;
};

// LLVM symbol names are usually ASCII, but can contain arbitrary bytes. We
// interpret the bytes as ISO-8859-1 and convert them to UTF-8 for use in map
// keys.
std::string bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes);
std::string bytesToUTF8(llvm::StringRef Bytes);
std::string utf8ToByteString(llvm::StringRef Str);

class memodb_value;

class memodb_ref {
private:
  std::vector<std::uint8_t> id_;
  enum {
    EMPTY,
    NUMERIC,
    INLINE_RAW,
    INLINE_DAG,
    BLAKE2B_RAW,
    BLAKE2B_MERKLEDAG
  } type_;
  friend class memodb_value;

public:
  memodb_ref() : id_(), type_(EMPTY) {}
  memodb_ref(llvm::StringRef Text);
  static memodb_ref fromCID(llvm::ArrayRef<std::uint8_t> Bytes);
  static memodb_ref loadCIDFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes);
  static memodb_ref fromBlake2BRaw(llvm::ArrayRef<std::uint8_t> Bytes);
  static memodb_ref fromBlake2BMerkleDAG(llvm::ArrayRef<std::uint8_t> Bytes);
  bool isCID() const { return type_ != EMPTY && type_ != NUMERIC; }
  bool isInline() const { return type_ == INLINE_RAW || type_ == INLINE_DAG; }
  bool isBlake2BRaw() const { return type_ == BLAKE2B_RAW; }
  bool isBlake2BMerkleDAG() const { return type_ == BLAKE2B_MERKLEDAG; }
  memodb_value asInline() const;
  llvm::ArrayRef<std::uint8_t> asBlake2BRaw() const;
  llvm::ArrayRef<std::uint8_t> asBlake2BMerkleDAG() const;
  std::vector<std::uint8_t> asCID() const;
  operator std::string() const;
  operator bool() const { return type_ != EMPTY; }
  bool operator<(const memodb_ref &other) const {
    if (type_ != other.type_)
      return type_ < other.type_;
    return id_ < other.id_;
  }
  bool operator==(const memodb_ref &other) const {
    return type_ == other.type_ && id_ == other.id_;
  }
};

std::ostream &operator<<(std::ostream &os, const memodb_ref &ref);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_ref &ref);

struct memodb_head {
  std::string Name;

  explicit memodb_head(const char *Name) : Name(Name) {}
  explicit memodb_head(std::string &&Name) : Name(Name) {}
  explicit memodb_head(llvm::StringRef Name) : Name(Name) {}
};

std::ostream &operator<<(std::ostream &os, const memodb_head &head);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_head &head);

struct memodb_call {
  std::string Name;
  std::vector<memodb_ref> Args;

  memodb_call(llvm::StringRef Name, llvm::ArrayRef<memodb_ref> Args)
      : Name(Name), Args(Args) {}
};

std::ostream &operator<<(std::ostream &os, const memodb_call &call);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_call &call);

struct memodb_name : public std::variant<memodb_ref, memodb_head, memodb_call> {
  typedef std::variant<memodb_ref, memodb_head, memodb_call> BaseType;

  constexpr memodb_name(const memodb_ref &Ref) : variant(Ref) {}
  constexpr memodb_name(const memodb_head &Head) : variant(Head) {}
  constexpr memodb_name(const memodb_call &Call) : variant(Call) {}
  constexpr memodb_name(memodb_ref &&Ref) : variant(Ref) {}
  constexpr memodb_name(memodb_head &&Head) : variant(Head) {}
  constexpr memodb_name(memodb_call &&Call) : variant(Call) {}

  template <class Visitor> constexpr void visit(Visitor &&vis) {
    BaseType &Base = *this;
    return std::visit(vis, Base);
  }

  template <class Visitor> constexpr void visit(Visitor &&vis) const {
    const BaseType &Base = *this;
    return std::visit(vis, Base);
  }
};

std::ostream &operator<<(std::ostream &os, const memodb_name &name);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_name &name);

class memodb_value {
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

  using value_type = memodb_value;
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
  using ref_t = memodb_ref;
  using array_t = std::vector<memodb_value>;
  using map_t = std::map<string_t, memodb_value>;

private:
  using variant_type = std::variant<null_t, bool_t, integer_t, float_t, bytes_t,
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

  void validateUTF8() const;

public:
  memodb_value() : variant_() {}
  memodb_value(std::nullptr_t) : variant_(null_t()) {}

  memodb_value(null_t val) : variant_(val) {}
  memodb_value(bool_t val) : variant_(val) {}
  memodb_value(integer_t val) : variant_(integer_t(val)) {}
  memodb_value(float_t val) : variant_(val) {}

  memodb_value(const bytes_t &val) : variant_(val) {}
  memodb_value(const string_t &val) : variant_(val) { validateUTF8(); }
  memodb_value(const ref_t &val) : variant_(val) {}
  memodb_value(const array_t &val) : variant_(val) {}
  memodb_value(const map_t &val) : variant_(val) {}

  memodb_value(bytes_t &&val) : variant_(std::move(val)) {}
  memodb_value(string_t &&val) : variant_(std::move(val)) { validateUTF8(); }
  memodb_value(ref_t &&val) : variant_(std::move(val)) {}
  memodb_value(array_t &&val) : variant_(std::move(val)) {}
  memodb_value(map_t &&val) : variant_(std::move(val)) {}

  template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
  memodb_value(T val) : variant_(integer_t(val)) {}
  template <typename T,
            std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
  memodb_value(T val) : variant_(float_t(val)) {}

  memodb_value(llvm::ArrayRef<std::uint8_t> val) : variant_(bytes_t(val)) {}
  static memodb_value bytes() { return memodb_value(bytes_t{}); }
  static memodb_value bytes(llvm::ArrayRef<char> val) {
    return memodb_value(llvm::ArrayRef<unsigned char>(
        reinterpret_cast<const unsigned char *>(val.begin()),
        reinterpret_cast<const unsigned char *>(val.end())));
  }
  static memodb_value bytes(llvm::StringRef val) {
    return memodb_value(
        llvm::ArrayRef<unsigned char>(val.bytes_begin(), val.bytes_end()));
  }

  memodb_value(const char *val) : variant_(string_t(val)) {}

  // Explicit because strings are often not valid UTF-8.
  static memodb_value string(llvm::StringRef val) {
    memodb_value result;
    result.variant_ = string_t(val);
    result.validateUTF8();
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

  static memodb_value load_cbor(llvm::ArrayRef<std::uint8_t> in) {
    return load_cbor_from_sequence(in);
  }
  static memodb_value load_cbor_from_sequence(llvm::ArrayRef<std::uint8_t> &in);
  void save_cbor(std::vector<std::uint8_t> &out) const;
  std::pair<memodb_ref, bytes_t> saveAsIPLD(bool noInline = false) const;

private:
  void require_type(value_t type) const {
    if (type != this->type()) {
      llvm::report_fatal_error("invalid memodb_value type");
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

using memodb_path = std::pair<memodb_name, std::vector<memodb_value>>;

class memodb_db {
public:
  virtual ~memodb_db() {}

  virtual llvm::Optional<memodb_value> getOptional(const memodb_name &name) = 0;
  virtual memodb_ref put(const memodb_value &value) = 0;
  virtual void set(const memodb_name &Name, const memodb_ref &ref) = 0;
  virtual std::vector<memodb_name> list_names_using(const memodb_ref &ref) = 0;
  virtual std::vector<std::string> list_funcs() = 0;
  // F should not modify the database. F can return true to stop iteration.
  virtual void eachHead(std::function<bool(const memodb_head &)> F) = 0;
  virtual void eachCall(llvm::StringRef Func,
                        std::function<bool(const memodb_call &)> F) = 0;
  virtual void head_delete(const memodb_head &Head) = 0;
  virtual void call_invalidate(llvm::StringRef name) = 0;

  virtual bool has(const memodb_name &name) {
    return getOptional(name).hasValue();
  }

  memodb_value get(const memodb_name &name) { return *getOptional(name); }

  memodb_ref head_get(llvm::StringRef name) {
    return get(memodb_head(name)).as_ref();
  }

  void head_set(llvm::StringRef name, const memodb_ref &ref) {
    set(memodb_head(name), ref);
  }

  memodb_ref call_get(llvm::StringRef name, llvm::ArrayRef<memodb_ref> args) {
    auto Value = getOptional(memodb_call(name, args));
    if (!Value)
      return memodb_ref();
    return Value->as_ref();
  }

  void call_set(llvm::StringRef name, llvm::ArrayRef<memodb_ref> args,
                const memodb_ref &result) {
    set(memodb_call(name, args), result);
  }

  std::vector<memodb_head> list_heads() {
    std::vector<memodb_head> Result;
    eachHead([&](const memodb_head &Head) {
      Result.emplace_back(Head);
      return false;
    });
    return Result;
  }

  std::vector<memodb_call> list_calls(llvm::StringRef Func) {
    std::vector<memodb_call> Result;
    eachCall(Func, [&](const memodb_call &Call) {
      Result.emplace_back(Call);
      return false;
    });
    return Result;
  }

  virtual std::vector<memodb_path> list_paths_to(const memodb_ref &ref);

  template <typename F, typename... Targs>
  memodb_ref call_or_lookup_ref(llvm::StringRef name, F func, Targs... Fargs) {
    memodb_ref ref = call_get(name, {Fargs...});
    if (!ref) {
      ref = put(func(*this, get(Fargs)...));
      call_set(name, {Fargs...}, ref);
    }
    return ref;
  }

  template <typename F, typename... Targs>
  memodb_value call_or_lookup_value(llvm::StringRef name, F func,
                                    Targs... Fargs) {
    memodb_value value;
    if (memodb_ref ref = call_get(name, {Fargs...})) {
      value = get(ref);
    } else {
      value = func(*this, get(Fargs)...);
      call_set(name, {Fargs...}, put(value));
    }
    return value;
  }
};

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing = false);

#endif // MEMODB_MEMODB_H
