#include "memodb/memodb.h"

#include "memodb_internal.h"

#include <llvm/Support/ErrorHandling.h>

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing) {
  if (uri.startswith("sqlite:")) {
    return memodb_sqlite_open(uri.substr(7), create_if_missing);
  } else {
    llvm::report_fatal_error(llvm::Twine("unsupported URI ") + uri);
  }
}

std::ostream &operator<<(std::ostream &os, const memodb_value &value) {
  bool first = true;
  switch (value.type_) {
  case memodb_value::UNDEFINED:
    return os << "undefined";
  case memodb_value::NULL_TYPE:
    return os << "null";
  case memodb_value::BOOL:
    return os << (value.bool_ ? "true" : "false");
  case memodb_value::INTEGER:
    return os << value.integer_;
  case memodb_value::BYTES:
    // TODO
    return os << "bytes";
  case memodb_value::STRING:
    // TODO: escape
    return os << '"' << value.string_ << '"';
  case memodb_value::REF:
    // TODO: escape
    return os << "39(\"" << llvm::StringRef(value.ref_).str() << "\")";
  case memodb_value::ARRAY:
    os << '[';
    for (const auto &item : value.array_) {
      if (!first)
        os << ',';
      first = false;
      os << item;
    }
    return os << ']';
  case memodb_value::MAP:
    os << '{';
    for (const auto &item : value.map_) {
      if (!first)
        os << ',';
      first = false;
      os << item.first << ':' << item.second;
    }
    return os << '}';
  }
  return os << "UNKNOWN";
}

memodb_value memodb_value::load_cbor_ref(llvm::ArrayRef<std::uint8_t> &in) {
  auto start = [&](int &major_type, int &minor_type, std::uint64_t &additional,
                   bool &indefinite) {
    major_type = in.front() >> 5;
    minor_type = in.front() & 0x1f;
    in = in.drop_front();

    indefinite = false;
    additional = 0;
    if (minor_type < 24) {
      additional = minor_type;
    } else if (minor_type < 28) {
      int num_bytes = 1 << (minor_type - 24);
      while (num_bytes--) {
        additional = additional << 8 | in.front();
        in = in.drop_front();
      }
    } else if (minor_type == 31) {
      indefinite = true;
    } else {
      assert(false);
    }
  };

  int major_type, minor_type;
  bool indefinite;
  std::uint64_t additional;
  bool in_middle_of_string = false;
  auto next_string = [&] {
    if (!indefinite && in_middle_of_string)
      return false;
    in_middle_of_string = true;
    if (indefinite) {
      if (in.front() == 0xff) {
        in = in.drop_front();
        return false;
      }
      int inner_major_type, inner_minor_type;
      bool inner_indefinite;
      start(inner_major_type, inner_minor_type, additional, inner_indefinite);
      assert(inner_major_type == major_type);
      assert(!inner_indefinite);
      return true;
    } else {
      return true;
    }
  };

  auto next_item = [&] {
    if (indefinite) {
      if (in.front() == 0xff) {
        in = in.drop_front();
        return false;
      }
      return true;
    } else {
      return additional-- > 0;
    }
  };

  bool is_ref = false;
  do {
    start(major_type, minor_type, additional, indefinite);
    if (major_type == 6 && additional == 39)
      is_ref = true;
  } while (major_type == 6);

  if (is_ref)
    assert(major_type == 3);

  memodb_value result;
  switch (major_type) {
  case 0:
    assert(!indefinite);
    return memodb_value(additional);
  case 1:
    assert(!indefinite);
    return memodb_value(-std::int64_t(additional) - 1);
  case 2:
    result.type_ = BYTES;
    while (next_string()) {
      result.bytes_.insert(result.bytes_.end(), in.data(),
                           in.data() + additional);
      in = in.drop_front(additional);
    }
    return result;
  case 3:
    result.type_ = STRING;
    while (next_string()) {
      result.string_.append(in.data(), in.data() + additional);
      in = in.drop_front(additional);
    }
    if (is_ref) {
      result.type_ = REF;
      result.ref_ = memodb_ref(std::move(result.string_));
    }
    return result;
  case 4:
    result.type_ = ARRAY;
    while (next_item())
      result.array_.emplace_back(load_cbor_ref(in));
    return result;
  case 5:
    result.type_ = MAP;
    while (next_item()) {
      memodb_value first = load_cbor_ref(in);
      memodb_value second = load_cbor_ref(in);
      result.map_[first] = second;
    }
    return result;
  case 7:
    assert(!indefinite);
    switch (minor_type) {
    case 20:
      result.type_ = BOOL;
      result.bool_ = false;
      return result;
    case 21:
      result.type_ = BOOL;
      result.bool_ = true;
      return result;
    case 22:
      result.type_ = NULL_TYPE;
      return result;
    case 23: // undefined
      return {};
    }
    assert(false);
  default:
    llvm_unreachable("impossible major type");
  }
}

void memodb_value::save_cbor(std::vector<std::uint8_t> &out) const {
  auto start = [&](int major_type, std::uint64_t additional) {
    int num_bytes;
    if (additional < 24) {
      out.push_back(major_type << 5 | additional);
      num_bytes = 0;
    } else if (additional < 0x100) {
      out.push_back(major_type << 5 | 24);
      num_bytes = 1;
    } else if (additional < 0x10000) {
      out.push_back(major_type << 5 | 25);
      num_bytes = 2;
    } else if (additional < 0x100000000) {
      out.push_back(major_type << 5 | 26);
      num_bytes = 4;
    } else {
      out.push_back(major_type << 5 | 27);
      num_bytes = 8;
    }
    for (int i = 0; i < num_bytes; i++)
      out.push_back((additional >> 8 * (num_bytes - i - 1)) & 0xff);
  };

  switch (type_) {
  case UNDEFINED:
    start(7, 23);
    break;
  case NULL_TYPE:
    start(7, 22);
    break;
  case BOOL:
    start(7, bool_ ? 21 : 20);
    break;
  case INTEGER:
    if (integer_ < 0)
      start(1, -integer_ - 1);
    else
      start(0, integer_);
    break;
  case BYTES:
    start(2, bytes_.size());
    out.insert(out.end(), bytes_.begin(), bytes_.end());
    break;
  case STRING:
    start(3, string_.size());
    out.insert(out.end(), string_.begin(), string_.end());
    break;
  case REF:
    start(6, 39); // "identifier" tag
    start(3, llvm::StringRef(ref_).size());
    out.insert(out.end(), llvm::StringRef(ref_).begin(),
               llvm::StringRef(ref_).end());
    break;
  case ARRAY:
    start(4, array_.size());
    for (const memodb_value &item : array_)
      item.save_cbor(out);
    break;
  case MAP:
    // use canonical CBOR order
    {
      std::vector<std::pair<bytes_t, const memodb_value *>> items;
      for (const auto &item : map_) {
        items.emplace_back(bytes_t(), &item.second);
        item.first.save_cbor(items.back().first);
      }
      std::sort(items.begin(), items.end(), [](auto &a, auto &b) {
        return a.first.size() != b.first.size()
                   ? a.first.size() < b.first.size()
                   : a.first < b.first;
      });
      start(5, items.size());
      for (const auto &item : items) {
        out.insert(out.end(), item.first.begin(), item.first.end());
        item.second->save_cbor(out);
      }
    }
    break;
  default:
    llvm_unreachable("missing switch case");
  }
}
