#include "memodb/Node.h"

#include <limits>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>
#include <system_error>

#include "memodb/CBOREncoder.h"
#include "memodb/JSONEncoder.h"
#include "memodb/Multibase.h"

using namespace memodb;

using std::int64_t;
using std::uint64_t;

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

Node &Node::operator=(std::nullptr_t) { return *this = Node(nullptr); }

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

bool Node::operator==(const Node &other) const { return compare(other) == 0; }

bool Node::operator!=(const Node &other) const { return compare(other) != 0; }

bool Node::operator<(const Node &other) const { return compare(other) < 0; }

bool Node::operator<=(const Node &other) const { return compare(other) <= 0; }

bool Node::operator>(const Node &other) const { return compare(other) > 0; }

bool Node::operator>=(const Node &other) const { return compare(other) >= 0; }

int Node::compare(const Node &other) const {
  Kind kind_left = kind(), kind_right = other.kind();
  if (kind_left < kind_right)
    return -1;
  else if (kind_left > kind_right)
    return 1;
  if (kind_left == Kind::Integer) {
    bool signed_left = is<std::int64_t>();
    bool signed_right = other.is<std::int64_t>();
    if (signed_left && signed_right) {
      auto left = as<std::int64_t>(), right = as<std::int64_t>();
      return left < right ? -1 : left == right ? 0 : 1;
    } else if (!signed_left && !signed_right) {
      auto left = as<std::uint64_t>(), right = as<std::uint64_t>();
      return left < right ? -1 : left == right ? 0 : 1;
    } else {
      return signed_left ? -1 : 1;
    }
  }
  return variant_ < other.variant_ ? -1 : variant_ == other.variant_ ? 0 : 1;
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
                        [](const std::uint64_t &) { return Kind::Integer; },
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

void Node::erase(const llvm::StringRef &name) {
  std::get<Map>(variant_).erase(name);
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

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os,
                                      const Node &value) {
  JSONEncoder(os).visitNode(value);
  return os;
}

std::ostream &memodb::operator<<(std::ostream &os, const Node &value) {
  llvm::raw_os_ostream raw_os(os);
  raw_os << value;
  return os;
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

std::vector<std::uint8_t> Node::saveAsCBOR() const {
  std::vector<std::uint8_t> out;
  CBOREncoder(out).visitNode(*this);
  return out;
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
  if (CID.getContentType() == Multicodec::DAG_CBOR ||
      CID.getContentType() == Multicodec::DAG_CBOR_Unrestricted)
    return Node::loadFromCBOR(Content);
  return createUnsupportedIPLDError("unsupported CID content type");
}

std::pair<CID, std::vector<std::uint8_t>>
Node::saveAsIPLD(bool noIdentity) const {
  bool raw = kind() == Kind::Bytes;
  std::vector<std::uint8_t> Bytes;
  Multicodec codec;
  if (raw) {
    Bytes = llvm::ArrayRef<std::uint8_t>(std::get<BytesStorage>(variant_));
    codec = Multicodec::Raw;
  } else {
    CBOREncoder encoder(Bytes);
    encoder.visitNode(*this);
    codec = encoder.isValidDAGCBOR() ? Multicodec::DAG_CBOR
                                     : Multicodec::DAG_CBOR_Unrestricted;
  }
  CID Ref = CID::calculate(codec, Bytes,
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

static void skipJSONSpace(llvm::StringRef &json) {
  json = json.ltrim(" \t\r\n");
}

static llvm::Expected<llvm::json::Value>
consumeJSONString(llvm::StringRef &json) {
  skipJSONSpace(json);
  if (!json.startswith("\""))
    return createInvalidJSONError("Expected '\"'");
  size_t i = 1;
  while (i < json.size() && json[i] != '"') {
    i = json.find_first_of("\"\\", i);
    if (i < json.size() && json[i] == '\\')
      i += 2;
  }
  if (i >= json.size())
    return createInvalidJSONError("Invalid string");
  i += 1;
  auto valueOrErr = llvm::json::parse(json.take_front(i));
  json = json.drop_front(i);
  return valueOrErr;
}

static llvm::Expected<Node> consumeJSON(llvm::StringRef &json,
                                        bool special_object = true) {
  // llvm::json::parse doesn't support the full range of uint64_t. It also
  // creates an intermediate data structure llvm::json::Value that we don't
  // really need. So we parse the JSON ourselves.
  skipJSONSpace(json);
  if (json.empty())
    return createInvalidJSONError("Missing value");
  char c = json.front();
  if (c == 'f') {
    if (json.consume_front("false"))
      return Node(false);
  } else if (c == 't') {
    if (json.consume_front("true"))
      return Node(true);
  } else if (c == 'n') {
    if (json.consume_front("null"))
      return Node(nullptr);
  } else if (c == '-') {
    if (json.consume_front("-0"))
      return Node(0);
    int64_t value;
    // MemoDB JSON allows only integers.
    if (json.consumeInteger(10, value))
      return createInvalidJSONError("Invalid integer");
    return Node(value);
  } else if (c == '0') {
    json = json.drop_front();
    return Node(0);
  } else if (c >= '1' && c <= '9') {
    uint64_t value;
    // MemoDB JSON allows only integers.
    if (json.consumeInteger(10, value))
      return createInvalidJSONError("Invalid integer");
    return Node(value);
  } else if (c == '"') {
    auto valueOrErr = consumeJSONString(json);
    if (!valueOrErr)
      return valueOrErr.takeError();
    return Node(utf8_string_arg, *valueOrErr->getAsString());
  } else if (c == '[') {
    Node result(node_list_arg);
    json = json.drop_front();
    skipJSONSpace(json);
    if (!json.startswith("]")) {
      do {
        auto valueOrErr = consumeJSON(json);
        if (!valueOrErr)
          return valueOrErr.takeError();
        result.emplace_back(std::move(*valueOrErr));
        skipJSONSpace(json);
      } while (json.consume_front(","));
    }
    if (!json.consume_front("]"))
      return createInvalidJSONError("Unexpected character");
    return result;
  } else if (c == '{') {
    Node result(node_map_arg);
    json = json.drop_front();
    skipJSONSpace(json);
    if (!json.startswith("}")) {
      do {
        auto keyOrErr = consumeJSONString(json);
        if (!keyOrErr)
          return keyOrErr.takeError();
        skipJSONSpace(json);
        if (!json.consume_front(":"))
          return createInvalidJSONError("Expected ':' in object");
        auto valueOrErr = consumeJSON(json, !special_object);
        if (!valueOrErr)
          return valueOrErr.takeError();
        auto emplaced = result.try_emplace(*keyOrErr->getAsString(),
                                           std::move(*valueOrErr));
        if (!emplaced.second)
          return createInvalidJSONError("Duplicate key in map");
        skipJSONSpace(json);
      } while (json.consume_front(","));
    }
    if (!json.consume_front("}"))
      return createInvalidJSONError("Expected '}'");
    if (!special_object)
      return result;
    if (result.size() != 1)
      return createInvalidJSONError("Invalid special object");
    auto &item = *result.map_range().begin();
    if (item.key() == "map" && item.value().is_map()) {
      return std::move(item.value());
    }
    if (item.key() == "cid" && item.value().is<llvm::StringRef>()) {
      auto value = item.value().as<llvm::StringRef>();
      if (!value.startswith("u"))
        return createInvalidJSONError("JSON CIDs must be base64url");
      auto cid_or_err = CID::parse(value);
      if (!cid_or_err)
        return createInvalidJSONError("Invalid or unsupported CID");
      return Node(std::move(*cid_or_err));
    }
    if (item.key() == "base64" && item.value().is<llvm::StringRef>()) {
      auto value = item.value().as<llvm::StringRef>();
      auto bytes = Multibase::base64pad.decodeWithoutPrefix(value);
      if (!bytes)
        return createInvalidJSONError("Invalid base64");
      return Node(std::move(*bytes));
    }
    if (item.key() == "float" && item.value().is<llvm::StringRef>()) {
      auto value = item.value().as<llvm::StringRef>();
      if (value == "NaN")
        return Node(NAN);
      if (value == "Infinity")
        return Node(INFINITY);
      if (value == "-Infinity")
        return Node(-INFINITY);
      double d;
      if (value.getAsDouble(d))
        return createInvalidJSONError("Invalid float");
      return Node(d);
    }
    return createInvalidJSONError("Invalid special JSON object");
  }
  return createInvalidJSONError("Unexpected character");
}

llvm::Expected<Node> Node::loadFromJSON(llvm::StringRef json) {
  auto valueOrErr = consumeJSON(json);
  if (!valueOrErr)
    return valueOrErr.takeError();
  skipJSONSpace(json);
  if (!json.empty())
    return createInvalidJSONError("Extra characters after JSON value");
  return *valueOrErr;
}
