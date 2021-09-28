#include "memodb/JSONEncoder.h"

#include <dragonbox/dragonbox.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"

using namespace memodb;

using std::int64_t;
using std::uint64_t;

JSONEncoder::JSONEncoder(llvm::raw_ostream &os) : os(os) {}

JSONEncoder::~JSONEncoder() {}

void JSONEncoder::visitNode(const Node &value) {
  if (!first)
    os << ',';
  first = false;
  NodeVisitor::visitNode(value);
}

void JSONEncoder::visitNull() { os << "null"; }

void JSONEncoder::visitBoolean(bool value) { os << (value ? "true" : "false"); }

void JSONEncoder::visitUInt64(std::uint64_t value) { os << value; }

void JSONEncoder::visitInt64(std::int64_t value) { os << value; }

void JSONEncoder::visitFloat(double value) {
  os << "{\"float\":\"";
  // https://tc39.es/ecma262/#sec-numeric-types-number-tostring
  // exception: -0.0 is printed as "-0"
  if (std::isnan(value)) {
    os << "NaN\"}";
    return;
  }
  if (std::signbit(value)) {
    os << '-';
    value = -value;
  }
  if (std::isinf(value)) {
    os << "Infinity\"}";
    return;
  }
  if (value == 0.0) {
    os << "0\"}";
    return;
  }

  auto decimal =
      jkj::dragonbox::to_decimal(value, jkj::dragonbox::policy::sign::ignore);
  llvm::SmallString<20> s;
  llvm::raw_svector_ostream(s) << decimal.significand;
  auto k = static_cast<int>(s.size());
  auto n = decimal.exponent + k;
  static const llvm::StringRef zeros("00000000000000000000");
  if (k <= n && n <= 21) {
    os << s << zeros.take_front(n - k);
  } else if (n > 0 && n <= 21) {
    os << s.substr(0, n) << '.' << s.substr(n);
  } else if (n <= 0 && n > -6) {
    os << "0." << zeros.take_front(-n) << s;
  } else {
    os << s[0];
    if (k > 1)
      os << '.' << s.substr(1);
    os << 'e' << (n >= 1 ? "+" : "") << (n - 1);
  }
  os << "\"}";
}

void JSONEncoder::visitString(llvm::StringRef value) {
  os << '"';
  for (char c : value) {
    if (c >= 0x08 && c <= 0x0d && c != 0x0b) {
      os << '\\' << "btn.fr"[c - 0x08];
    } else if (c >= 0 && c < 0x20) {
      os << "\\u" << llvm::format_hex_no_prefix(c, 4);
    } else {
      if (c == '\\' || c == '"')
        os << '\\';
      os << c;
    }
  }
  os << '"';
}

void JSONEncoder::visitBytes(BytesRef value) {
  os << "{\"base64\":\"" << Multibase::base64pad.encodeWithoutPrefix(value)
     << "\"}";
}

void JSONEncoder::visitLink(const CID &value) {
  os << "{\"cid\":\"" << value.asString(Multibase::base64url) << "\"}";
}

void JSONEncoder::startList(const Node::List &value) {
  os << '[';
  first = true;
}

void JSONEncoder::endList() {
  os << ']';
  first = false;
}

void JSONEncoder::startMap(const Node::Map &value) {
  os << "{\"map\":{";
  first = true;
}

void JSONEncoder::visitKey(llvm::StringRef value) {
  if (!first)
    os << ',';
  visitString(value);
  os << ':';
  first = true;
}

void JSONEncoder::endMap() {
  os << "}}";
  first = false;
}
