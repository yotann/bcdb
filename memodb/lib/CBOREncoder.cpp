#include "memodb/CBOREncoder.h"

#include <cmath>
#include <limits>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"

using namespace memodb;

using std::int64_t;
using std::uint64_t;
using std::uint8_t;

CBOREncoder::CBOREncoder(std::vector<uint8_t> &out) : out(out) {}

CBOREncoder::~CBOREncoder() {}

bool CBOREncoder::hasLinks() const { return has_links; }

bool CBOREncoder::isValidDAGCBOR() const { return !not_dag_cbor; }

void CBOREncoder::encodeHead(int major_type, uint64_t argument,
                             int force_additional) {
  int num_bytes;
  if (force_additional == 0 && argument < 24) {
    out.push_back(major_type << 5 | argument);
    num_bytes = 0;
  } else if (force_additional ? force_additional == 24 : argument < 0x100) {
    out.push_back(major_type << 5 | 24);
    num_bytes = 1;
  } else if (force_additional ? force_additional == 25 : argument < 0x10000) {
    out.push_back(major_type << 5 | 25);
    num_bytes = 2;
  } else if (force_additional ? force_additional == 26
                              : argument < 0x100000000) {
    out.push_back(major_type << 5 | 26);
    num_bytes = 4;
  } else {
    out.push_back(major_type << 5 | 27);
    num_bytes = 8;
  }
  for (int i = 0; i < num_bytes; i++)
    out.push_back((argument >> 8 * (num_bytes - i - 1)) & 0xff);
}

void CBOREncoder::visitNull() { encodeHead(7, 22); }

void CBOREncoder::visitBoolean(bool value) { encodeHead(7, value ? 21 : 20); }

void CBOREncoder::visitUInt64(std::uint64_t value) {
  encodeHead(0, value);
  if (value > uint64_t(std::numeric_limits<int64_t>::max()))
    not_dag_cbor = true;
}

void CBOREncoder::visitInt64(std::int64_t value) {
  if (value < 0)
    encodeHead(1, -(value + 1));
  else
    encodeHead(0, value);
}

bool CBOREncoder::encodeFloat(uint64_t &result, double value, int total_size,
                              int mantissa_size, int exponent_bias) {
  uint64_t exponent_mask = (1ull << (total_size - mantissa_size - 1)) - 1;
  int exponent;
  uint64_t mantissa;
  bool exact = true;
  bool sign = std::signbit(value);
  if (std::isnan(value)) {
    // This is the default NaN on most platforms.
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

void CBOREncoder::visitFloat(double value) {
  if (!std::isfinite(value))
    not_dag_cbor = true;
  uint64_t argument;
  // DAG-CBOR allows only 64-bit floats.
  if (false && encodeFloat(argument, value, 16, 10, 15))
    return encodeHead(7, argument, 25);
  if (false && encodeFloat(argument, value, 32, 23, 127))
    return encodeHead(7, argument, 26);
  encodeFloat(argument, value, 64, 52, 1023);
  encodeHead(7, argument, 27);
}

void CBOREncoder::visitString(llvm::StringRef value) {
  encodeHead(3, value.size());
  out.insert(out.end(), value.begin(), value.end());
}

void CBOREncoder::visitBytes(BytesRef value) {
  encodeHead(2, value.size());
  out.insert(out.end(), value.begin(), value.end());
}

void CBOREncoder::visitLink(const Link &value) {
  // https://github.com/ipld/cid-cbor/
  auto bytes = value.getCID().asBytes();
  encodeHead(6, 42); // CID tag
  encodeHead(2, bytes.size() + 1);
  out.push_back(0x00); // DAG-CBOR requires multibase prefix
  out.insert(out.end(), bytes.begin(), bytes.end());
  has_links = true;
}

void CBOREncoder::startList(const Node::List &value) {
  encodeHead(4, value.size());
}

void CBOREncoder::startMap(const Node::Map &value) {
  encodeHead(5, value.size());
}
