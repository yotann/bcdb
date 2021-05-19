#include "memodb/memodb.h"

#include "memodb_internal.h"

#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_os_ostream.h>
#include <sstream>

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing) {
  if (uri.startswith("sqlite:")) {
    return memodb_sqlite_open(uri.substr(7), create_if_missing);
  } else if (uri.startswith("leveldb:")) {
    return memodb_leveldb_open(uri, create_if_missing);
  } else {
    llvm::report_fatal_error(llvm::Twine("unsupported URI ") + uri);
  }
}

std::ostream &operator<<(std::ostream &os, const memodb_ref &ref) {
  return os << llvm::StringRef(ref).str();
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_ref &ref) {
  return os << llvm::StringRef(ref);
}

std::ostream &operator<<(std::ostream &os, const memodb_head &head) {
  return os << head.Name;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_head &head) {
  return os << head.Name;
}

std::ostream &operator<<(std::ostream &os, const memodb_call &call) {
  llvm::raw_os_ostream(os) << call;
  return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_call &call) {
  os << call.Name << "(";
  bool first = true;
  for (const memodb_ref &Arg : call.Args) {
    if (!first)
      os << ", ";
    first = false;
    os << Arg;
  }
  return os << ")";
}

std::ostream &operator<<(std::ostream &os, const memodb_name &name) {
  llvm::raw_os_ostream(os) << name;
  return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_name &name) {
  if (const memodb_head *Head = std::get_if<memodb_head>(&name)) {
    os << "heads[" << memodb_value(Head->Name) << "]";
  } else {
    name.visit([&](auto X) { os << X; });
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, const memodb_value &value) {
  // Print the value in CBOR extended diagnostic notation.
  // https://tools.ietf.org/html/rfc8610#appendix-G

  auto print_escaped = [&](llvm::StringRef str, char quote) {
    for (char c : str) {
      if (c == '\\' || c == quote)
        os << '\\' << c;
      else if (c == '\n')
        os << "\\n";
      else if (c == '\r')
        os << "\\r";
      else if (c == '\t')
        os << "\\t";
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
                  return (b >= 32 && b <= 126) || b == '\n' || b == '\r' ||
                         b == '\t';
                })) {
              // NOTE: implementations are inconsistent about how ' and "
              // should be escaped. We escape ' as \' and leave " as-is.
              os << "'";
              print_escaped(value.as_bytestring(), '\'');
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
            print_escaped(x, '"');
            os << '"';
          },
          [&](const memodb_value::ref_t &x) {
            os << "39(\"";
            print_escaped(x, '"');
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

memodb_value
memodb_value::load_cbor_from_sequence(llvm::ArrayRef<std::uint8_t> &in) {
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
      llvm::report_fatal_error("Invalid minor type");
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
      if (inner_major_type != major_type)
        llvm::report_fatal_error("Invalid indefinite-length string");
      if (inner_indefinite)
        llvm::report_fatal_error("Invalid nested indefinite-length strings");
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

  if (is_ref && major_type != 3)
    llvm::report_fatal_error("Invalid reference type");

  switch (major_type) {
  case 0:
    if (indefinite)
      llvm::report_fatal_error("Integers may not be indefinite");
    return additional;
  case 1:
    if (indefinite)
      llvm::report_fatal_error("Integers may not be indefinite");
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
      result.emplace_back(load_cbor_from_sequence(in));
    return result;
  }
  case 5: {
    map_t result;
    while (next_item()) {
      memodb_value key = load_cbor_from_sequence(in);
      result[key] = load_cbor_from_sequence(in);
    }
    return result;
  }
  case 7:
    if (indefinite)
      llvm::report_fatal_error("Simple values may not be indefinite");
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
    llvm::report_fatal_error("Unsupported simple value");
  default:
    llvm_unreachable("impossible major type");
  }
}

void memodb_value::save_cbor(std::vector<std::uint8_t> &out) const {
  // Save the value in deterministically encoded CBOR format.
  // https://www.rfc-editor.org/rfc/rfc8949.html#name-deterministically-encoded-c

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
                   std::sort(items.begin(), items.end());
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
  auto listPathsWithin =
      [](const memodb_value &Value,
         const memodb_ref &Ref) -> std::vector<std::vector<memodb_value>> {
    std::vector<std::vector<memodb_value>> Result;
    std::vector<memodb_value> CurPath;
    std::function<void(const memodb_value &)> recurse =
        [&](const memodb_value &Value) {
          if (Value.type() == memodb_value::REF) {
            if (Value.as_ref() == Ref)
              Result.push_back(CurPath);
          } else if (Value.type() == memodb_value::ARRAY) {
            for (size_t i = 0; i < Value.array_items().size(); i++) {
              CurPath.push_back(i);
              recurse(Value[i]);
              CurPath.pop_back();
            }
          } else if (Value.type() == memodb_value::MAP) {
            for (const auto &item : Value.map_items()) {
              CurPath.push_back(item.first);
              recurse(item.second);
              CurPath.pop_back();
            }
          }
        };
    recurse(Value);
    return Result;
  };

  std::vector<memodb_path> Result;
  std::vector<memodb_value> BackwardsPath;
  std::function<void(const memodb_ref &)> recurse = [&](const memodb_ref &Ref) {
    for (const auto &Parent : list_names_using(Ref)) {
      if (const memodb_ref *ParentRef = std::get_if<memodb_ref>(&Parent)) {
        const memodb_value Value = get(*ParentRef);
        for (const auto &Subpath : listPathsWithin(Value, Ref)) {
          BackwardsPath.insert(BackwardsPath.end(), Subpath.rbegin(),
                               Subpath.rend());
          recurse(*ParentRef);
          BackwardsPath.erase(BackwardsPath.end() - Subpath.size(),
                              BackwardsPath.end());
        }
      } else {
        Result.emplace_back(Parent,
                            std::vector<memodb_value>(BackwardsPath.rbegin(),
                                                      BackwardsPath.rend()));
      }
    }
  };
  recurse(ref);
  return Result;
}
