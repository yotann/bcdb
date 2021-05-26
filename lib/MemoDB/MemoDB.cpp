#include "memodb/memodb.h"

#include "memodb_internal.h"

#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_os_ostream.h>
#include <sodium.h>
#include <sstream>

ParsedURI::ParsedURI(llvm::StringRef URI) {
  llvm::StringRef AuthorityRef, PathRef, QueryRef, FragmentRef;

  if (URI.contains(':'))
    std::tie(Scheme, URI) = URI.split(':');
  if (URI.startswith("//")) {
    size_t i = URI.find_first_of("/?#", 2);
    if (i == llvm::StringRef::npos) {
      AuthorityRef = URI.substr(2);
      URI = "";
    } else {
      AuthorityRef = URI.substr(2, i - 2);
      URI = URI.substr(i);
    }
  }
  std::tie(URI, FragmentRef) = URI.split('#');
  std::tie(PathRef, QueryRef) = URI.split('?');

  auto percentDecode = [](llvm::StringRef Str) -> std::string {
    if (!Str.contains('%'))
      return Str.str();
    std::string Result;
    while (!Str.empty()) {
      size_t i = Str.find('%');
      Result.append(Str.take_front(i));
      Str = Str.substr(i);
      if (Str.empty())
        break;
      unsigned Code;
      if (Str.size() >= 3 && !Str.substr(1, 2).getAsInteger(16, Code)) {
        Result.push_back((char)Code);
        Str = Str.substr(3);
      } else {
        llvm::report_fatal_error("invalid percent encoding in URI");
      }
    }
    return Result;
  };

  Authority = percentDecode(AuthorityRef);
  Path = percentDecode(PathRef);
  Query = percentDecode(QueryRef);
  Fragment = percentDecode(FragmentRef);

  llvm::SmallVector<llvm::StringRef, 8> Segments;
  PathRef.split(Segments, '/');
  for (const auto &Segment : Segments)
    PathSegments.emplace_back(percentDecode(Segment));
}

std::string bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes) {
  std::string Result;
  for (std::uint8_t Byte : Bytes) {
    if (Byte < 0x80) {
      Result.push_back((char)Byte);
    } else {
      Result.push_back((char)(0xc0 | (Byte >> 6)));
      Result.push_back((char)(0x80 | (Byte & 0x3f)));
    }
  }
  return Result;
}

std::string bytesToUTF8(llvm::StringRef Bytes) {
  return bytesToUTF8(llvm::ArrayRef(
      reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
}

std::string utf8ToByteString(llvm::StringRef Str) {
  std::string Result;
  while (!Str.empty()) {
    std::uint8_t x = (std::uint8_t)Str[0];
    if (x < 0x80) {
      Result.push_back((char)x);
      Str = Str.drop_front(1);
    } else {
      std::uint8_t y = Str.size() >= 2 ? (std::uint8_t)Str[1] : 0;
      if ((x & 0xfc) != 0xc0 || (y & 0xc0) != 0x80)
        llvm::report_fatal_error("invalid UTF-8 bytes");
      Result.push_back((char)((x & 3) << 6 | (y & 0x3f)));
      Str = Str.drop_front(2);
    }
  }
  return Result;
}

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

// IDs (except legacy numeric IDs) follow the CID specification:
// https://github.com/multiformats/cid

static const char *BASE32_PREFIX = "b";

static const llvm::StringRef BASE32_TABLE("abcdefghijklmnopqrstuvwxyz234567");

static const size_t HASH_SIZE = crypto_generichash_BYTES;

static const std::array<std::uint8_t, 3> CID_PREFIX_INLINE_RAW = {
    0x01, // CIDv1
    0x55, // content type: raw
    0x00, // multihash function: identity
};

static const std::array<std::uint8_t, 3> CID_PREFIX_INLINE_DAG = {
    0x01, // CIDv1
    0x71, // content type: MerkleDAG CBOR
    0x00, // multihash function: identity
};

static const std::array<std::uint8_t, 6> CID_PREFIX_RAW = {
    0x01,                  // CIDv1
    0x55,                  // content type: raw
    0xa0,      0xe4, 0x02, // multihash function: Blake2B-256
    HASH_SIZE,             // multihash size
};

static const std::array<std::uint8_t, 6> CID_PREFIX_DAG = {
    0x01,                  // CIDv1
    0x71,                  // content type: MerkleDAG CBOR
    0xa0,      0xe4, 0x02, // multihash function: Blake2B-256
    HASH_SIZE,             // multihash size
};

static std::vector<std::uint8_t> decodeBase32(llvm::StringRef Str) {
  std::vector<std::uint8_t> Result;
  while (!Str.empty()) {
    uint64_t Value = 0;
    for (size_t i = 0; i < 8; i++) {
      Value <<= 5;
      if (i < Str.size()) {
        size_t Pos = BASE32_TABLE.find(Str[i]);
        if (Pos == llvm::StringRef::npos)
          llvm::report_fatal_error("invalid character in base32");
        Value |= Pos;
      }
    }

    const size_t NumBytesArray[8] = {0, 0, 1, 0, 2, 3, 0, 4};
    size_t NumBytes = Str.size() >= 8 ? 5 : NumBytesArray[Str.size()];
    if (!NumBytes)
      llvm::report_fatal_error("invalid length of base32");
    for (size_t i = 0; i < NumBytes; i++)
      Result.push_back((Value >> (32 - 8 * i)) & 0xff);
    Str = Str.drop_front(Str.size() >= 8 ? 8 : Str.size());
  }
  return Result;
}

static std::string encodeBase32(llvm::ArrayRef<std::uint8_t> Bytes) {
  std::string Result;
  while (!Bytes.empty()) {
    uint64_t Value = 0;
    for (size_t i = 0; i < 5; i++)
      Value = (Value << 8) | (i < Bytes.size() ? Bytes[i] : 0);
    const size_t NumCharsArray[5] = {0, 2, 4, 5, 7};
    size_t NumChars = Bytes.size() >= 5 ? 8 : NumCharsArray[Bytes.size()];
    for (size_t i = 0; i < NumChars; i++)
      Result.push_back(BASE32_TABLE[(Value >> (35 - 5 * i)) & 0x1f]);
    Bytes = Bytes.drop_front(Bytes.size() >= 5 ? 5 : Bytes.size());
  }
  return Result;
}

memodb_ref::memodb_ref(llvm::StringRef Text) {
  if (Text.startswith(BASE32_PREFIX)) {
    *this = fromCID(decodeBase32(Text.drop_front()));
  } else if (Text.find_first_not_of("0123456789") == llvm::StringRef::npos) {
    id_ = llvm::ArrayRef(reinterpret_cast<const std::uint8_t *>(Text.data()),
                         Text.size());
    type_ = NUMERIC;
  } else {
    llvm::report_fatal_error(llvm::Twine("invalid ID format ") + Text);
  }
}

memodb_ref memodb_ref::fromCID(llvm::ArrayRef<std::uint8_t> Bytes) {
  auto startsWith = [&](const auto &Prefix) {
    return Bytes.take_front(Prefix.size()).equals(Prefix);
  };
  if (startsWith(CID_PREFIX_RAW)) {
    return fromBlake2BRaw(Bytes.drop_front(CID_PREFIX_RAW.size()));
  } else if (startsWith(CID_PREFIX_DAG)) {
    return fromBlake2BMerkleDAG(Bytes.drop_front(CID_PREFIX_DAG.size()));
  } else if (startsWith(CID_PREFIX_INLINE_RAW)) {
    Bytes = Bytes.drop_front(CID_PREFIX_INLINE_RAW.size());
    if (Bytes.empty() || Bytes[0] >= 0x80 || Bytes[0] != Bytes.size() - 1)
      llvm::report_fatal_error("invalid inline CID");
    memodb_ref Result;
    Result.id_ = Bytes.drop_front(1);
    Result.type_ = INLINE_RAW;
    return Result;
  } else if (startsWith(CID_PREFIX_INLINE_DAG)) {
    Bytes = Bytes.drop_front(CID_PREFIX_INLINE_DAG.size());
    if (Bytes.empty() || Bytes[0] >= 0x80 || Bytes[0] != Bytes.size() - 1)
      llvm::report_fatal_error("invalid inline CID");
    memodb_ref Result;
    Result.id_ = Bytes.drop_front(1);
    Result.type_ = INLINE_DAG;
    return Result;
  } else {
    llvm::report_fatal_error(llvm::Twine("invalid CID"));
  }
}

memodb_ref memodb_ref::fromBlake2BRaw(llvm::ArrayRef<std::uint8_t> Bytes) {
  if (Bytes.size() != HASH_SIZE)
    llvm::report_fatal_error("incorrect Blake2B hash size");
  memodb_ref Result;
  Result.id_ = Bytes;
  Result.type_ = BLAKE2B_RAW;
  return Result;
}

memodb_ref
memodb_ref::fromBlake2BMerkleDAG(llvm::ArrayRef<std::uint8_t> Bytes) {
  if (Bytes.size() != HASH_SIZE)
    llvm::report_fatal_error("incorrect Blake2B hash size");
  memodb_ref Result;
  Result.id_ = Bytes;
  Result.type_ = BLAKE2B_MERKLEDAG;
  return Result;
}

memodb_value memodb_ref::asInline() const {
  if (type_ == INLINE_RAW)
    return memodb_value(id_);
  else if (type_ == INLINE_DAG)
    return memodb_value::load_cbor(id_);
  else
    llvm::report_fatal_error("incorrect type of ID");
}

llvm::ArrayRef<std::uint8_t> memodb_ref::asBlake2BRaw() const {
  if (type_ != BLAKE2B_RAW)
    llvm::report_fatal_error("incorrect type of ID");
  return id_;
}

llvm::ArrayRef<std::uint8_t> memodb_ref::asBlake2BMerkleDAG() const {
  if (type_ != BLAKE2B_MERKLEDAG)
    llvm::report_fatal_error("incorrect type of ID");
  return id_;
}

std::vector<std::uint8_t> memodb_ref::asCID() const {
  std::vector<std::uint8_t> Result;
  if (type_ == BLAKE2B_RAW)
    Result.assign(CID_PREFIX_RAW.begin(), CID_PREFIX_RAW.end());
  else if (type_ == BLAKE2B_MERKLEDAG)
    Result.assign(CID_PREFIX_DAG.begin(), CID_PREFIX_DAG.end());
  else if (type_ == INLINE_RAW) {
    Result.assign(CID_PREFIX_INLINE_RAW.begin(), CID_PREFIX_INLINE_RAW.end());
    Result.push_back(id_.size());
  } else if (type_ == INLINE_DAG) {
    Result.assign(CID_PREFIX_INLINE_DAG.begin(), CID_PREFIX_INLINE_DAG.end());
    Result.push_back(id_.size());
  } else
    llvm::report_fatal_error("ID not a CID");
  Result.insert(Result.end(), id_.begin(), id_.end());
  return Result;
}

memodb_ref::operator std::string() const {
  if (type_ == EMPTY)
    return "";
  else if (type_ == NUMERIC)
    return std::string(reinterpret_cast<const char *>(id_.data()), id_.size());
  else {
    auto Bytes = asCID();
    return BASE32_PREFIX + encodeBase32(Bytes);
  }
}

std::ostream &operator<<(std::ostream &os, const memodb_ref &ref) {
  return os << std::string(ref);
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_ref &ref) {
  return os << std::string(ref);
}

std::ostream &operator<<(std::ostream &os, const memodb_head &head) {
  return os << head.Name;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_head &head) {
  return os << head.Name;
}

std::ostream &operator<<(std::ostream &os, const memodb_call &call) {
  os << "call:" << call.Name;
  for (const memodb_ref &Arg : call.Args)
    os << "/" << Arg;
  return os;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const memodb_call &call) {
  os << "call:" << call.Name;
  for (const memodb_ref &Arg : call.Args)
    os << "/" << Arg;
  return os;
}

std::ostream &operator<<(std::ostream &os, const memodb_name &name) {
  if (const memodb_head *Head = std::get_if<memodb_head>(&name)) {
    os << "heads[" << memodb_value(Head->Name) << "]";
  } else {
    name.visit([&](auto X) { os << X; });
  }
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

void memodb_value::validateUTF8() const {
  auto Str = as<string_t>();
  auto Ptr = reinterpret_cast<const llvm::UTF8 *>(Str.data());
  if (!llvm::isLegalUTF8String(&Ptr, Ptr + Str.size()))
    llvm::report_fatal_error("invalid UTF-8 in string value");
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
            if (x.isCID()) {
              os << "42(h'00"; // include multibase prefix
              for (std::uint8_t b : x.asCID()) {
                char buf[3];
                std::snprintf(buf, sizeof(buf), "%02x", b);
                os << buf;
              }
              os << "')";
            } else {
              os << "39(\"";
              print_escaped(std::string(x), '"');
              os << "\")";
            }
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
              os << '"';
              print_escaped(item.first, '"');
              os << "\": " << item.second;
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

  bool is_numeric_ref = false, is_cid = false;
  do {
    start(major_type, minor_type, additional, indefinite);
    if (major_type == 6 && additional == 39)
      is_numeric_ref = true;
    if (major_type == 6 && additional == 42)
      is_cid = true;
  } while (major_type == 6);

  if (is_cid && major_type != 2)
    llvm::report_fatal_error("Invalid CID type");
  if (is_numeric_ref && major_type != 3)
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
    if (is_cid) {
      if (result.empty() || result[0] != 0x00)
        llvm::report_fatal_error("invalid encoded CID");
      return memodb_ref::fromCID(llvm::ArrayRef(result).drop_front(1));
    }
    return memodb_value(result);
  }
  case 3: {
    string_t result;
    while (next_string()) {
      result.append(in.data(), in.data() + additional);
      in = in.drop_front(additional);
    }
    if (is_numeric_ref)
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
      std::string KeyString;
      if (key.type() == memodb_value::STRING)
        KeyString = key.as_string();
      else if (key.type() == memodb_value::BYTES)
        KeyString = bytesToUTF8(key.as_bytes());
      else if (key.type() == memodb_value::ARRAY) {
        // Needed for legacy smout.collated values.
        std::vector<std::uint8_t> KeyBytes;
        key.save_cbor(KeyBytes);
        KeyString = bytesToUTF8(KeyBytes);
      } else
        llvm::report_fatal_error("Map keys must be strings");
      result[KeyString] = load_cbor_from_sequence(in);
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
      return nullptr;
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
  // Save the value in DAG-CBOR format.
  // https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-cbor.md

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
                   encode_float(additional, x, 64, 52, 1023);
                   start(7, additional, 27);
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
                   if (x.isCID()) {
                     auto Bytes = x.asCID();
                     // Insert identity multibase prefix (required by DAG-CBOR).
                     Bytes.insert(Bytes.begin(), 0x00);
                     start(6, 42); // CID tag
                     start(2, Bytes.size());
                     out.insert(out.end(), Bytes.begin(), Bytes.end());
                   } else {
                     std::string Str = x;
                     start(6, 39); // "identifier" tag
                     start(3, Str.size());
                     out.insert(out.end(), Str.begin(), Str.end());
                   }
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
                     memodb_value(item.first).save_cbor(items.back().first);
                   }
                   std::sort(items.begin(), items.end(),
                             [](const auto &A, const auto &B) {
                               if (A.first.size() != B.first.size())
                                 return A.first.size() < B.first.size();
                               return A.first < B.first;
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

std::pair<memodb_ref, memodb_value::bytes_t>
memodb_value::saveAsIPLD(bool noInline) const {
  bool raw = type() == BYTES;
  bytes_t Bytes;
  if (raw)
    Bytes = as_bytes();
  else
    save_cbor(Bytes);
  if (!noInline && CID_PREFIX_INLINE_DAG.size() + 1 + Bytes.size() <=
                       CID_PREFIX_DAG.size() + HASH_SIZE) {
    memodb_ref Ref;
    Ref.id_ = std::move(Bytes);
    Ref.type_ = raw ? memodb_ref::INLINE_RAW : memodb_ref::INLINE_DAG;
    return {Ref, {}};
  } else {
    memodb_ref Ref;
    Ref.id_.resize(HASH_SIZE);
    Ref.type_ = raw ? memodb_ref::BLAKE2B_RAW : memodb_ref::BLAKE2B_MERKLEDAG;
    crypto_generichash(Ref.id_.data(), Ref.id_.size(), Bytes.data(),
                       Bytes.size(), nullptr, 0);
    return {std::move(Ref), std::move(Bytes)};
  }
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
