#include "memodb/Node.h"

#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>
#include <system_error>

#include "memodb/Multibase.h"

using namespace memodb;

void Node::validateUTF8() const {
  auto Str = std::get<StringStorage>(variant_);
  auto Ptr = reinterpret_cast<const llvm::UTF8 *>(Str.data());
  if (!llvm::isLegalUTF8String(&Ptr, Ptr + Str.size()))
    llvm::report_fatal_error("invalid UTF-8 in string value");
}

void Node::validateKeysUTF8() const {
  auto map = std::get<Map>(variant_);
  for (const auto &Item : map) {
    auto Ptr = reinterpret_cast<const llvm::UTF8 *>(Item.key().data());
    if (!llvm::isLegalUTF8String(&Ptr, Ptr + Item.key().size()))
      llvm::report_fatal_error("invalid UTF-8 in string value");
  }
}

Node::Node() : variant_() {}

Node::Node(std::nullptr_t) : variant_() {}

Node::Node(bool val) : variant_(val) {}

Node::Node(double Float) : variant_(Float) {}

Node::Node(const char *Str) : variant_(StringStorage(Str)) { validateUTF8(); }

Node::Node(const char *Str, std::size_t Length)
    : variant_(StringStorage(Str, Str + Length)) {
  validateUTF8();
}

Node &Node::operator=(const char *Str) { return *this = Node(Str); }

Node::Node(UTF8StringArg, const llvm::StringRef &sr)
    : variant_(StringStorage(sr)) {
  validateUTF8();
}

Node::Node(UTF8StringArg, const std::string_view &sv)
    : variant_(StringStorage(sv.begin(), sv.end())) {
  validateUTF8();
}

Node::Node(UTF8StringArg, const std::string &str)
    : variant_(StringStorage(str)) {
  validateUTF8();
}

Node::Node(ByteStringArg) : variant_(BytesStorage()) {}

Node::Node(BytesRef bytes) : Node(byte_string_arg, bytes) {}

Node::Node(NodeListArg) : Node(List()) {}

Node::Node(NodeListArg, std::initializer_list<Node> init) : Node(List(init)) {}

Node::Node(const List &list) : variant_(list) {}
Node::Node(List &&list) : variant_(std::forward<List>(list)) {}

Node::Node(NodeMapArg) : Node(Map()) {}

Node::Node(NodeMapArg,
           std::initializer_list<std::pair<llvm::StringRef, Node>> init)
    : Node(Map(init)) {
  validateKeysUTF8();
}

Node::Node(const Map &map) : variant_(map) { validateKeysUTF8(); }
Node::Node(Map &&map) : variant_(std::forward<Map>(map)) { validateKeysUTF8(); }

Node::Node(const CID &val) : variant_(val) {}
Node::Node(CID &&val) : variant_(std::forward<CID>(val)) {}

bool Node::operator==(const Node &other) const {
  return variant_ == other.variant_;
}

bool Node::operator!=(const Node &other) const {
  return variant_ != other.variant_;
}

bool Node::operator<(const Node &other) const {
  return variant_ < other.variant_;
}

llvm::Expected<Node> Node::loadFromCBOR(BytesRef in) {
  auto result = loadFromCBORSequence(in);
  if (!result)
    return result.takeError();
  if (!in.empty())
    return llvm::createStringError(std::errc::invalid_argument,
                                   "Extra bytes after CBOR node");
  return *result;
}

Kind Node::kind() const {
  return std::visit(Overloaded{
                        [](const std::monostate &) { return Kind::Null; },
                        [](const bool &) { return Kind::Boolean; },
                        [](const std::int64_t &) { return Kind::Integer; },
                        [](const double &) { return Kind::Float; },
                        [](const StringStorage &) { return Kind::String; },
                        [](const BytesStorage &) { return Kind::Bytes; },
                        [](const List &) { return Kind::List; },
                        [](const Map &) { return Kind::Map; },
                        [](const CID &) { return Kind::Link; },
                    },
                    variant_);
}

std::size_t Node::size() const {
  return std::visit(
      Overloaded{
          [](const StringStorage &val) -> std::size_t { return val.size(); },
          [](const BytesStorage &val) -> std::size_t { return val.size(); },
          [](const List &List) -> std::size_t { return List.size(); },
          [](const Map &Map) -> std::size_t { return Map.size(); },
          [](const auto &X) -> std::size_t { return 0; },
      },
      variant_);
}

Node &Node::operator[](std::size_t i) { return at(i); }

const Node &Node::operator[](std::size_t i) const { return at(i); }

Node &Node::operator[](const llvm::StringRef &name) {
  return std::get<Map>(variant_).try_emplace(name).first->value();
}

const Node &Node::operator[](const llvm::StringRef &name) const {
  return at(name);
}

bool Node::contains(const llvm::StringRef &key) const noexcept {
  return count(key) != 0;
}

std::size_t Node::count(const llvm::StringRef &key) const {
  if (const Map *value = std::get_if<Map>(&variant_))
    return value->find(std::string(key)) != value->end() ? 1 : 0;
  return 0;
}

bool Node::empty() const noexcept {
  return std::visit(
      Overloaded{
          [](const StringStorage &str) { return str.empty(); },
          [](const std::vector<std::uint8_t> &bytes) { return bytes.empty(); },
          [](const List &list) { return list.empty(); },
          [](const Map &map) { return map.empty(); },
          [](const auto &) { return false; },
      },
      variant_);
}

void Node::resize(std::size_t n) {
  if (List *value = std::get_if<List>(&variant_))
    value->resize(n);
}

Node &Node::at(const llvm::StringRef &name) {
  auto &value = std::get<Map>(variant_);
  auto iter = value.find(name);
  if (iter == value.end())
    llvm::report_fatal_error("Key \"" + std::string(name) + "\" not found");
  return iter->value();
}

const Node &Node::at(const llvm::StringRef &name) const {
  const auto &value = std::get<Map>(variant_);
  auto iter = value.find(name);
  if (iter == value.end())
    llvm::report_fatal_error("Key \"" + std::string(name) + "\" not found");
  return iter->value();
}

Node &Node::at(std::size_t i) { return std::get<List>(variant_).at(i); }

const Node &Node::at(std::size_t i) const {
  return std::get<List>(variant_).at(i);
}

Node::Map::iterator Node::find(const llvm::StringRef &name) {
  return std::get<Map>(variant_).find(name);
}

Node::Map::const_iterator Node::find(const llvm::StringRef &name) const {
  return std::get<Map>(variant_).find(name);
}

const Node &Node::at_or_null(const llvm::StringRef &name) const {
  static const Node null_node = nullptr;
  const Map &value = std::get<Map>(variant_);
  auto iter = value.find(name);
  return iter == value.end() ? null_node : iter->value();
}

void Node::clear() {
  std::visit(Overloaded{
                 [](List &val) { val.clear(); },
                 [](Map &val) { val.clear(); },
                 [](auto &) {},
             },
             variant_);
}

Range<Node::Map::iterator> Node::map_range() {
  auto &value = std::get<Map>(variant_);
  return Range(value.begin(), value.end());
}

Range<Node::Map::const_iterator> Node::map_range() const {
  const auto &value = std::get<Map>(variant_);
  return Range(value.begin(), value.end());
}

Range<Node::List::iterator> Node::list_range() {
  auto &value = std::get<List>(variant_);
  return Range(value.begin(), value.end());
}

Range<Node::List::const_iterator> Node::list_range() const {
  const auto &value = std::get<List>(variant_);
  return Range(value.begin(), value.end());
}

static void writeJSON(llvm::json::OStream &os, const Node &value) {
  switch (value.kind()) {
  case Kind::Null:
    os.value(nullptr);
    break;
  case Kind::Boolean:
    os.value(value.as<bool>());
    break;
  case Kind::Integer:
    os.value(value.as<int64_t>());
    break;
  case Kind::Float:
    os.objectBegin();
    os.attributeBegin("float");
    os.rawValue([&value](llvm::raw_ostream &os) {
      os << '"'
         << llvm::format("%.*g", std::numeric_limits<double>::max_digits10,
                         value.as<double>())
         << '"';
    });
    os.attributeEnd();
    os.objectEnd();
    break;
  case Kind::String:
    os.value(value.as<llvm::StringRef>());
    break;
  case Kind::Bytes:
    os.objectBegin();
    os.attributeBegin("base64");
    os.value(Multibase::base64pad.encodeWithoutPrefix(value.as<BytesRef>()));
    os.attributeEnd();
    os.objectEnd();
    break;
  case Kind::List:
    os.arrayBegin();
    for (const Node &item : value.list_range())
      writeJSON(os, item);
    os.arrayEnd();
    break;
  case Kind::Map:
    os.objectBegin();
    os.attributeBegin("map");
    os.objectBegin();
    for (const auto &item : value.map_range()) {
      os.attributeBegin(item.key());
      writeJSON(os, item.value());
      os.attributeEnd();
    }
    os.objectEnd();
    os.attributeEnd();
    os.objectEnd();
    break;
  case Kind::Link:
    os.objectBegin();
    os.attributeBegin("cid");
    os.value(llvm::json::Value(value.as<CID>().asString(Multibase::base64url)));
    os.attributeEnd();
    os.objectEnd();
    break;
  }
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os,
                                      const Node &value) {
  llvm::json::OStream json_os(os);
  writeJSON(json_os, value);
  return os;
}

std::ostream &memodb::operator<<(std::ostream &os, const Node &value) {
  std::string str;
  llvm::raw_string_ostream sstr(str);
  sstr << value;
  return os << sstr.str();
}

static double decode_float(std::uint64_t value, int total_size,
                           int mantissa_size, int exponent_bias) {
  std::uint64_t exponent_mask = (1ull << (total_size - mantissa_size - 1)) - 1;
  std::uint64_t exponent = (value >> mantissa_size) & exponent_mask;
  std::uint64_t mantissa = value & ((1ull << mantissa_size) - 1);
  double result;
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

static bool encode_float(std::uint64_t &result, double value, int total_size,
                         int mantissa_size, int exponent_bias) {
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
    if (exponent >=
        static_cast<int>(exponent_mask)) { // too large, use infinity
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
      exact = static_cast<double>(mantissa) == value;
    }
  }
  result = (sign ? (1ull << (total_size - 1)) : 0) |
           (std::uint64_t(exponent) << mantissa_size) | mantissa;
  return exact;
}

static llvm::Error createInvalidCBORError(llvm::StringRef message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument),
      "Invalid CBOR: " + message);
}

static llvm::Error createUnsupportedCBORError(llvm::StringRef message) {
  return llvm::createStringError(std::make_error_code(std::errc::not_supported),
                                 "Unsupported CBOR: " + message);
}

llvm::Expected<Node>
Node::loadFromCBORSequence(llvm::ArrayRef<std::uint8_t> &in) {
  auto start = [&](int &major_type, int &minor_type, std::uint64_t &additional,
                   bool &indefinite) -> llvm::Error {
    if (in.empty())
      return createInvalidCBORError("unexpected end of input");
    major_type = in.front() >> 5;
    minor_type = in.front() & 0x1f;
    in = in.drop_front();

    indefinite = false;
    additional = 0;
    if (minor_type < 24) {
      additional = minor_type;
    } else if (minor_type < 28) {
      unsigned num_bytes = 1 << (minor_type - 24);
      if (in.size() < num_bytes)
        return createInvalidCBORError("truncated head");
      while (num_bytes--) {
        additional = additional << 8 | in.front();
        in = in.drop_front();
      }
    } else if (minor_type == 31 && major_type >= 2 && major_type <= 5) {
      indefinite = true;
    } else {
      return createInvalidCBORError("invalid minor type");
    }
    return llvm::Error::success();
  };

  int major_type, minor_type;
  bool indefinite;
  std::uint64_t additional;
  bool in_middle_of_string = false;

  auto next_string = [&]() -> llvm::Expected<bool> {
    if (!indefinite && in_middle_of_string)
      return false;
    in_middle_of_string = true;
    if (indefinite) {
      if (!in.empty() && in.front() == 0xff) {
        in = in.drop_front();
        return false;
      }
      int inner_major_type, inner_minor_type;
      bool inner_indefinite;
      auto error = start(inner_major_type, inner_minor_type, additional,
                         inner_indefinite);
      if (error)
        return std::move(error);
      if (inner_major_type != major_type || inner_indefinite)
        return createInvalidCBORError("invalid indefinite-length string");
      return true;
    } else {
      return true;
    }
  };

  auto next_item = [&] {
    if (indefinite) {
      if (!in.empty() && in.front() == 0xff) {
        in = in.drop_front();
        return false;
      }
      return true;
    } else {
      return additional-- > 0;
    }
  };

  bool is_cid = false;
  auto error = start(major_type, minor_type, additional, indefinite);
  if (error)
    return std::move(error);
  if (major_type == 6 && additional == 42) {
    is_cid = true;
    error = start(major_type, minor_type, additional, indefinite);
    if (error)
      return std::move(error);
  } else if (major_type == 6) {
    return createUnsupportedCBORError("unsupported tag");
  }

  if (is_cid && major_type != 2)
    return createInvalidCBORError("invalid kind in CID tag");

  switch (major_type) {
  case 0:
    return additional;
  case 1:
    if (additional > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
      return createUnsupportedCBORError("integer too large");
    return -std::int64_t(additional) - 1;
  case 2: {
    BytesStorage result;
    while (true) {
      auto next = next_string();
      if (!next)
        return next.takeError();
      if (!*next)
        break;
      if (in.size() < additional)
        return createInvalidCBORError("missing data from string");
      result.insert(result.end(), in.data(), in.data() + additional);
      in = in.drop_front(additional);
    }
    if (is_cid) {
      if (result.empty() || result[0] != 0x00)
        return createInvalidCBORError("missing CID prefix");
      auto CID =
          CID::fromBytes(llvm::ArrayRef<std::uint8_t>(result).drop_front(1));
      if (!CID)
        return createUnsupportedCBORError("unsupported or invalid CID");
      return Node(*CID);
    }
    Node node;
    node.variant_ = result;
    return node;
  }
  case 3: {
    StringStorage result;
    while (true) {
      auto next = next_string();
      if (!next)
        return next.takeError();
      if (!*next)
        break;
      if (in.size() < additional)
        return createInvalidCBORError("missing data from string");
      result.append(in.data(), in.data() + additional);
      in = in.drop_front(additional);
    }
    auto ptr = reinterpret_cast<const llvm::UTF8 *>(result.data());
    if (!llvm::isLegalUTF8String(&ptr, ptr + result.size()))
      return createInvalidCBORError("invalid UTF-8 in string value");
    return Node(utf8_string_arg, std::move(result));
  }
  case 4: {
    List result;
    while (next_item()) {
      auto item = loadFromCBORSequence(in);
      if (!item)
        return item.takeError();
      result.emplace_back(*item);
    }
    return result;
  }
  case 5: {
    Map result;
    while (next_item()) {
      auto key = loadFromCBORSequence(in);
      if (!key)
        return key.takeError();
      if (!key->is<llvm::StringRef>())
        return createUnsupportedCBORError("map keys must be strings");
      auto value = loadFromCBORSequence(in);
      if (!value)
        return value.takeError();
      result.insert_or_assign(key->as<llvm::StringRef>(), *value);
    }
    return result;
  }
  case 7:
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
    return createUnsupportedCBORError("unsupported simple value");
  default:
    llvm_unreachable("impossible major type");
  }
}

void Node::save_cbor(std::vector<std::uint8_t> &out) const {
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

  std::visit(Overloaded{
                 [&](const std::monostate &) { start(7, 22); },
                 [&](const bool &x) { start(7, x ? 21 : 20); },
                 [&](const std::int64_t &x) {
                   if (x < 0)
                     start(1, -(x + 1));
                   else
                     start(0, x);
                 },
                 [&](const double &x) {
                   std::uint64_t additional;
                   encode_float(additional, x, 64, 52, 1023);
                   start(7, additional, 27);
                 },
                 [&](const BytesStorage &x) {
                   start(2, x.size());
                   out.insert(out.end(), x.begin(), x.end());
                 },
                 [&](const StringStorage &x) {
                   start(3, x.size());
                   out.insert(out.end(), x.begin(), x.end());
                 },
                 [&](const CID &x) {
                   auto Bytes = x.asBytes();
                   start(6, 42); // CID tag
                   start(2, Bytes.size() + 1);
                   out.push_back(0x00); // DAG-CBOR requires multibase prefix
                   out.insert(out.end(), Bytes.begin(), Bytes.end());
                 },
                 [&](const List &x) {
                   start(4, x.size());
                   for (const Node &item : x)
                     item.save_cbor(out);
                 },
                 [&](const Map &x) {
                   start(5, x.size());
                   for (const auto &item : x) {
                     start(3, item.key().size());
                     out.insert(out.end(), item.key().begin(),
                                item.key().end());
                     item.value().save_cbor(out);
                   }
                 },
             },
             variant_);
}

static llvm::Error createInvalidIPLDError(llvm::StringRef message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument),
      "Invalid IPLD block: " + message);
}

static llvm::Error createUnsupportedIPLDError(llvm::StringRef message) {
  return llvm::createStringError(std::make_error_code(std::errc::not_supported),
                                 "Unsupported IPLD block: " + message);
}

llvm::Expected<Node> Node::loadFromIPLD(const CID &CID,
                                        llvm::ArrayRef<std::uint8_t> Content) {
  if (CID.isIdentity()) {
    if (!Content.empty())
      return createInvalidIPLDError("identity CID should have empty payload");
    Content = CID.getHashBytes();
  }
  if (CID.getContentType() == Multicodec::Raw)
    return Node(Content);
  if (CID.getContentType() == Multicodec::DAG_CBOR)
    return Node::loadFromCBOR(Content);
  return createUnsupportedIPLDError("unsupported CID content type");
}

std::pair<CID, std::vector<std::uint8_t>>
Node::saveAsIPLD(bool noIdentity) const {
  bool raw = kind() == Kind::Bytes;
  std::vector<std::uint8_t> Bytes;
  if (raw)
    Bytes = llvm::ArrayRef<std::uint8_t>(std::get<BytesStorage>(variant_));
  else
    save_cbor(Bytes);
  CID Ref = CID::calculate(raw ? Multicodec::Raw : Multicodec::DAG_CBOR, Bytes,
                           noIdentity ? std::optional(Multicodec::Blake2b_256)
                                      : std::nullopt);
  if (Ref.isIdentity())
    return {std::move(Ref), {}};
  else
    return {std::move(Ref), std::move(Bytes)};
}

static llvm::Error createInvalidJSONError(llvm::StringRef message) {
  return llvm::createStringError(
      std::make_error_code(std::errc::invalid_argument),
      "Invalid MemoDB JSON: " + message);
}

static llvm::Expected<Node> loadFromJSONValue(const llvm::json::Value &value) {
  switch (value.kind()) {
  case llvm::json::Value::Null:
    return Node(nullptr);
  case llvm::json::Value::Boolean:
    return Node(*value.getAsBoolean());
  case llvm::json::Value::Number:
    return Node(*value.getAsInteger());
  case llvm::json::Value::String:
    return Node(utf8_string_arg, *value.getAsString());
  case llvm::json::Value::Array: {
    Node result(node_list_arg);
    for (const auto &item : *value.getAsArray()) {
      auto nodeOrErr = loadFromJSONValue(item);
      if (!nodeOrErr)
        return nodeOrErr.takeError();
      result.emplace_back(std::move(*nodeOrErr));
    }
    return result;
  }
  case llvm::json::Value::Object: {
    const auto &outer = *value.getAsObject();
    if (outer.size() == 1) {
      if (auto f = outer.getString("float")) {
        double d;
        if (f->getAsDouble(d))
          return createInvalidJSONError("Invalid float");
        return Node(d);
      }
      if (auto base64 = outer.getString("base64")) {
        auto bytes = Multibase::base64pad.decodeWithoutPrefix(*base64);
        if (!bytes)
          return createInvalidJSONError("Invalid base64");
        return Node(std::move(*bytes));
      }
      if (auto cid_str = outer.getString("cid")) {
        if (!cid_str->startswith("u"))
          return createInvalidJSONError("JSON CIDs must be base64url");
        auto cid_or_err = CID::parse(*cid_str);
        if (!cid_or_err)
          return createInvalidJSONError("Invalid or unsupported CID");
        return Node(std::move(*cid_or_err));
      }
      if (auto inner = outer.getObject("map")) {
        Node result(node_map_arg);
        for (const auto &item : *inner) {
          auto valueOrErr = loadFromJSONValue(item.second);
          if (!valueOrErr)
            return valueOrErr.takeError();
          result[item.first] = *valueOrErr;
        }
        return result;
      }
    }
    return createInvalidJSONError("Invalid special JSON object");
  }
  default:
    llvm_unreachable("impossible JSON type");
  }
}

llvm::Expected<Node> Node::loadFromJSON(llvm::StringRef json) {
  auto valueOrErr = llvm::json::parse(json);
  if (!valueOrErr)
    return valueOrErr.takeError();
  return loadFromJSONValue(*valueOrErr);
}