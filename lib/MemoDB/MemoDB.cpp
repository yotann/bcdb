#include "memodb/memodb.h"

#include "memodb_internal.h"

#include <llvm/Support/ErrorHandling.h>
#include <sstream>

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing) {
  if (uri.startswith("sqlite:")) {
    return memodb_sqlite_open(uri.substr(7), create_if_missing);
  } else {
    llvm::report_fatal_error(llvm::Twine("unsupported URI ") + uri);
  }
}

std::ostream &operator<<(std::ostream &os, const memodb_value &value) {
  // Print the value in CBOR extended diagnostic notation.
  // https://tools.ietf.org/html/rfc8610#appendix-G

  auto print_escaped = [&](llvm::StringRef str) {
    for (char c : str) {
      if (c == '\\' || c == '"')
        os << '\\' << c;
      else if ((unsigned char)c < 0x20 || c == 0x7f) {
        char buf[5];
        std::snprintf(buf, sizeof(buf), "%04x", (unsigned int)(unsigned char)c);
        os << "\\u" << buf;
      } else
        os << c;
    }
  };

  std::visit(
      overloaded{
          [&](const memodb_value::undefined_t &) { os << "undefined"; },
          [&](const memodb_value::null_t &) { os << "null"; },
          [&](const memodb_value::bool_t &x) { os << (x ? "true" : "false"); },
          [&](const memodb_value::integer_t &x) { os << x; },
          [&](const memodb_value::float_t &x) {
            if (std::isnan(x))
              os << "NaN";
            else if (std::isinf(x))
              os << (x < 0 ? "-Infinity" : "Infinity");
            else
              os << x;
          },
          [&](const memodb_value::bytes_t &x) {
            if (std::all_of(x.begin(), x.end(), [](std::uint8_t b) {
                  return b >= 33 && b <= 126 && b != '\'' && b != '"' &&
                         b != '\\';
                })) {
              os << "'";
              for (std::uint8_t b : x)
                os << static_cast<char>(b);
              os << "'";
            } else {
              os << "h'";
              for (std::uint8_t b : x) {
                char buf[3];
                std::snprintf(buf, sizeof(buf), "%02x", b);
                os << buf;
              }
              os << "'";
            }
          },
          [&](const memodb_value::string_t &x) {
            os << '"';
            print_escaped(x);
            os << '"';
          },
          [&](const memodb_value::ref_t &x) {
            os << "39(\"";
            print_escaped(x);
            os << "\")";
          },
          [&](const memodb_value::array_t &x) {
            bool first = true;
            os << '[';
            for (const auto &item : x) {
              if (!first)
                os << ", ";
              first = false;
              os << item;
            }
            os << ']';
          },
          [&](const memodb_value::map_t &x) {
            bool first = true;
            os << '{';
            for (const auto &item : x) {
              if (!first)
                os << ", ";
              first = false;
              os << item.first << ": " << item.second;
            }
            os << '}';
          },
      },
      value.variant_);
  return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const memodb_value &value) {
  std::stringstream sstr;
  sstr << value;
  os << sstr.str();
  return os;
}

static memodb_value::float_t decode_float(std::uint64_t value, int total_size,
                                          int mantissa_size,
                                          int exponent_bias) {
  std::uint64_t exponent_mask = (1ull << (total_size - mantissa_size - 1)) - 1;
  std::uint64_t exponent = (value >> mantissa_size) & exponent_mask;
  std::uint64_t mantissa = value & ((1ull << mantissa_size) - 1);
  memodb_value::float_t result;
  if (exponent == 0)
    result =
        std::ldexp(mantissa, 1 - (mantissa_size + exponent_bias)); // denormal
  else if (exponent == exponent_mask)
    result = mantissa == 0 ? INFINITY : NAN;
  else
    result = std::ldexp(mantissa + (1ull << mantissa_size),
                        exponent - (mantissa_size + exponent_bias));
  return value & (1ull << (total_size - 1)) ? -result : result;
}

static bool encode_float(std::uint64_t &result, memodb_value::float_t value,
                         int total_size, int mantissa_size, int exponent_bias) {
  std::uint64_t exponent_mask = (1ull << (total_size - mantissa_size - 1)) - 1;
  int exponent;
  std::uint64_t mantissa;
  bool exact = true;
  bool sign = std::signbit(value);
  if (std::isnan(value)) {
    exponent = exponent_mask;
    mantissa = 1ull << (mantissa_size - 1);
    sign = false;
  } else if (std::isinf(value)) {
    exponent = exponent_mask;
    mantissa = 0;
  } else if (value == 0.0) {
    exponent = 0;
    mantissa = 0;
  } else {
    value = std::frexp(std::abs(value), &exponent);
    exponent += exponent_bias - 1;
    if (exponent >= (int)exponent_mask) { // too large, use infinity
      exponent = exponent_mask;
      mantissa = 0;
      exact = false;
    } else {
      if (exponent <= 0) { // denormal
        value = std::ldexp(value, exponent);
        exponent = 0;
      } else {
        value = std::ldexp(value, 1) - 1.0;
      }
      value = std::ldexp(value, mantissa_size);
      mantissa = (std::uint64_t)value;
      exact = (memodb_value::float_t)mantissa == value;
    }
  }
  result = (sign ? (1ull << (total_size - 1)) : 0) |
           (std::uint64_t(exponent) << mantissa_size) | mantissa;
  return exact;
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

  switch (major_type) {
  case 0:
    assert(!indefinite);
    return additional;
  case 1:
    assert(!indefinite);
    return -std::int64_t(additional) - 1;
  case 2: {
    bytes_t result;
    while (next_string()) {
      result.insert(result.end(), in.data(), in.data() + additional);
      in = in.drop_front(additional);
    }
    return memodb_value(result);
  }
  case 3: {
    string_t result;
    while (next_string()) {
      result.append(in.data(), in.data() + additional);
      in = in.drop_front(additional);
    }
    if (is_ref)
      return memodb_ref(std::move(result));
    return memodb_value::string(result);
  }
  case 4: {
    array_t result;
    while (next_item())
      result.emplace_back(load_cbor_ref(in));
    return result;
  }
  case 5: {
    map_t result;
    while (next_item()) {
      memodb_value key = load_cbor_ref(in);
      result[key] = load_cbor_ref(in);
    }
    return result;
  }
  case 7:
    assert(!indefinite);
    switch (minor_type) {
    case 20:
      return false;
    case 21:
      return true;
    case 22:
      return nullptr;
    case 23: // undefined
      return {};
    case 25:
      return decode_float(additional, 16, 10, 15);
    case 26:
      return decode_float(additional, 32, 23, 127);
    case 27:
      return decode_float(additional, 64, 52, 1023);
    }
    assert(false);
  default:
    llvm_unreachable("impossible major type");
  }
}

void memodb_value::save_cbor(std::vector<std::uint8_t> &out) const {
  auto start = [&](int major_type, std::uint64_t additional,
                   int force_minor = 0) {
    int num_bytes;
    if (force_minor == 0 && additional < 24) {
      out.push_back(major_type << 5 | additional);
      num_bytes = 0;
    } else if (force_minor ? force_minor == 24 : additional < 0x100) {
      out.push_back(major_type << 5 | 24);
      num_bytes = 1;
    } else if (force_minor ? force_minor == 25 : additional < 0x10000) {
      out.push_back(major_type << 5 | 25);
      num_bytes = 2;
    } else if (force_minor ? force_minor == 26 : additional < 0x100000000) {
      out.push_back(major_type << 5 | 26);
      num_bytes = 4;
    } else {
      out.push_back(major_type << 5 | 27);
      num_bytes = 8;
    }
    for (int i = 0; i < num_bytes; i++)
      out.push_back((additional >> 8 * (num_bytes - i - 1)) & 0xff);
  };

  std::visit(overloaded{
                 [&](const undefined_t &) { start(7, 23); },
                 [&](const null_t &) { start(7, 22); },
                 [&](const bool_t &x) { start(7, x ? 21 : 20); },
                 [&](const integer_t &x) {
                   if (x < 0)
                     start(1, -(x + 1));
                   else
                     start(0, x);
                 },
                 [&](const float_t &x) {
                   std::uint64_t additional;
                   if (encode_float(additional, x, 16, 10, 15))
                     start(7, additional, 25);
                   else if (encode_float(additional, x, 32, 23, 127))
                     start(7, additional, 26);
                   else {
                     encode_float(additional, x, 64, 52, 1023);
                     start(7, additional, 27);
                   }
                 },
                 [&](const bytes_t &x) {
                   start(2, x.size());
                   out.insert(out.end(), x.begin(), x.end());
                 },
                 [&](const string_t &x) {
                   start(3, x.size());
                   out.insert(out.end(), x.begin(), x.end());
                 },
                 [&](const ref_t &x) {
                   start(6, 39); // "identifier" tag
                   start(3, llvm::StringRef(x).size());
                   out.insert(out.end(), llvm::StringRef(x).begin(),
                              llvm::StringRef(x).end());
                 },
                 [&](const array_t &x) {
                   start(4, x.size());
                   for (const memodb_value &item : x)
                     item.save_cbor(out);
                 },
                 [&](const map_t &x) {
                   std::vector<std::pair<bytes_t, const memodb_value *>> items;
                   for (const auto &item : x) {
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
                     out.insert(out.end(), item.first.begin(),
                                item.first.end());
                     item.second->save_cbor(out);
                   }
                 },
             },
             variant_);
}

std::vector<memodb_path> memodb_db::list_paths_to(const memodb_ref &ref) {
  auto list_paths_within =
      [](const memodb_value &value,
         const memodb_ref &ref) -> std::vector<memodb_path> {
    std::vector<memodb_path> result;
    memodb_path cur_path;
    std::function<void(const memodb_value &)> recurse =
        [&](const memodb_value &value) {
          if (value.type() == memodb_value::REF) {
            if (value.as_ref() == ref)
              result.push_back(cur_path);
          } else if (value.type() == memodb_value::ARRAY) {
            for (size_t i = 0; i < value.array_items().size(); i++) {
              cur_path.push_back(i);
              recurse(value[i]);
              cur_path.pop_back();
            }
          } else if (value.type() == memodb_value::MAP) {
            for (const auto &item : value.map_items()) {
              cur_path.push_back(item.first);
              recurse(item.second);
              cur_path.pop_back();
            }
          }
        };
    recurse(value);
    return result;
  };

  std::vector<memodb_path> result;
  memodb_path backwards_path;
  std::function<void(const memodb_ref &)> recurse = [&](const memodb_ref &ref) {
    for (const auto &head : list_heads_using(ref)) {
      backwards_path.push_back(memodb_value::string(head));
      result.emplace_back(backwards_path.rbegin(), backwards_path.rend());
      backwards_path.pop_back();
    }
    for (const auto &parent : list_refs_using(ref)) {
      const memodb_value value = get(parent);
      for (const memodb_path &subpath : list_paths_within(value, ref)) {
        backwards_path.insert(backwards_path.end(), subpath.rbegin(),
                              subpath.rend());
        recurse(parent);
        backwards_path.erase(backwards_path.end() - subpath.size(),
                             backwards_path.end());
      }
    }
  };
  recurse(ref);
  return result;
}
