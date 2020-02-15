#ifndef MEMODB_MEMODB_H
#define MEMODB_MEMODB_H

#include <iosfwd>
#include <map>
#include <memory>
#include <stddef.h>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>

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

  using bool_t = bool;
  using integer_t = std::int64_t;
  using bytes_t = std::vector<std::uint8_t>;
  using string_t = std::string;
  using ref_t = memodb_ref;
  using array_t = std::vector<memodb_value>;
  using map_t = std::map<memodb_value, memodb_value>;

private:
  value_t type_;

  // TODO: use union
  bool_t bool_;
  integer_t integer_;
  bytes_t bytes_;
  string_t string_;
  ref_t ref_;
  array_t array_;
  map_t map_;

public:
  memodb_value() : type_(UNDEFINED) {}
  memodb_value(std::nullptr_t) : type_(NULL_TYPE) {}

  memodb_value(bool_t val) : type_(BOOL), bool_(val) {}

  memodb_value(int val) : type_(INTEGER), integer_(val) {}
  memodb_value(integer_t val) : type_(INTEGER), integer_(val) {}
  memodb_value(std::uint64_t val) : type_(INTEGER), integer_(val) {}

  memodb_value(llvm::ArrayRef<std::uint8_t> val) : type_(BYTES), bytes_(val) {}
  static memodb_value bytes(llvm::StringRef val) {
    return memodb_value(
        llvm::ArrayRef<unsigned char>(val.bytes_begin(), val.bytes_end()));
  }

  // TODO: verify that value is valid UTF-8.
  memodb_value(const char *val) : type_(STRING), string_(val) {}
  // Explicit because strings are often not valid UTF-8.
  static memodb_value string(llvm::StringRef val);

  memodb_value(const memodb_ref &val) : type_(REF), ref_(val) {}

  static memodb_value
  array(std::initializer_list<array_t::value_type> init = {}) {
    memodb_value result;
    result.type_ = ARRAY;
    result.array_ = array_t(init);
    return result;
  }
  template <class InputIT>
  static memodb_value array(InputIT first, InputIT last);

  static memodb_value map(std::initializer_list<map_t::value_type> init = {}) {
    memodb_value result;
    result.type_ = MAP;
    result.map_ = map_t(init);
    return result;
  }
  template <class InputIT> static memodb_value map(InputIT first, InputIT last);

  const ref_t &as_ref() const {
    require_type(REF);
    return ref_;
  }
  const llvm::ArrayRef<std::uint8_t> as_bytes() const { return bytes_; }

  map_t &map_items() {
    require_type(MAP);
    return map_;
  }

  constexpr value_t type() const noexcept { return type_; }

  static memodb_value load_cbor(llvm::ArrayRef<std::uint8_t> in) {
    return load_cbor_ref(in);
  }
  void save_cbor(std::vector<std::uint8_t> &out) const;

private:
  static memodb_value load_cbor_ref(llvm::ArrayRef<std::uint8_t> &in);

  void require_type(value_t type) const {
    if (type != type_) {
      llvm::report_fatal_error("invalid memodb_value type");
    }
  }

public:
  reference at(const value_type &idx) {
    if (type_ == ARRAY) {
      idx.require_type(INTEGER);
      return array_.at(idx.integer_);
    } else {
      require_type(MAP);
      return map_.at(idx);
    }
  }

  const_reference at(const value_type &idx) const {
    if (type_ == ARRAY) {
      idx.require_type(INTEGER);
      return array_.at(idx.integer_);
    } else {
      require_type(MAP);
      return map_.at(idx);
    }
  }

  reference operator[](const value_type &idx) {
    if (type_ == ARRAY) {
      return at(idx);
    } else {
      require_type(MAP);
      return map_[idx];
    }
  }

  const_reference operator[](const value_type &idx) const {
    if (type_ == ARRAY) {
      return at(idx);
    } else {
      assert(map_.find(idx) != map_.end());
      return map_.find(idx)->second;
    }
  }

  bool operator<(const memodb_value &other) const {
    if (type_ < other.type_)
      return true;
    if (type_ > other.type_)
      return false;
    switch (type_) {
    case UNDEFINED:
      return false;
    case NULL_TYPE:
      return false;
    case BOOL:
      return bool_ < other.bool_;
    case INTEGER:
      return integer_ < other.integer_;
    case BYTES:
      return bytes_ < other.bytes_;
    case STRING:
      return string_ < other.string_;
    case REF:
      return ref_ < other.ref_;
    case ARRAY:
      return array_ < other.array_;
    case MAP:
      return map_ < other.map_;
    }
    llvm_unreachable("missing switch case");
  }

  bool operator==(const memodb_value &other) const {
    if (type_ != other.type_)
      return false;
    switch (type_) {
    case UNDEFINED:
    case NULL_TYPE:
      return true;
    case BOOL:
      return bool_ == other.bool_;
    case INTEGER:
      return integer_ == other.integer_;
    case BYTES:
      return bytes_ == other.bytes_;
    case STRING:
      return string_ == other.string_;
    case REF:
      return ref_ == other.ref_;
    case ARRAY:
      return array_ == other.array_;
    case MAP:
      return map_ == other.map_;
    }
    llvm_unreachable("missing switch case");
  }

  friend std::ostream &operator<<(std::ostream &, const memodb_value &);
};

std::ostream &operator<<(std::ostream &os, const memodb_value &value);

class memodb_db {
public:
  virtual ~memodb_db() {}

  virtual memodb_value get(const memodb_ref &ref) = 0;
  virtual memodb_ref put(const memodb_value &value) = 0;

  virtual std::vector<std::string> list_heads() = 0;
  virtual memodb_ref head_get(llvm::StringRef name) = 0;
  virtual void head_set(llvm::StringRef name, const memodb_ref &ref) = 0;
  virtual void head_delete(llvm::StringRef name) = 0;

  virtual memodb_ref call_get(llvm::StringRef name,
                              llvm::ArrayRef<memodb_ref> args) = 0;
  virtual void call_set(llvm::StringRef name, llvm::ArrayRef<memodb_ref> args,
                        const memodb_ref &result) = 0;
  virtual void call_invalidate(llvm::StringRef name) = 0;
};

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing = false);

#endif // MEMODB_MEMODB_H
