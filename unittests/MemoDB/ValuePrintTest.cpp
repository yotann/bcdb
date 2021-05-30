#include "memodb/memodb.h"

#include "gtest/gtest.h"
#include <sstream>

namespace {

void test_print(llvm::StringRef expected, const memodb_value &value) {
  std::stringstream out;
  out << value;
  EXPECT_EQ(expected, out.str());
}

TEST(ValuePrintTest, Integer) {
  test_print("0", memodb_value(0));
  test_print("1", memodb_value(1));
  test_print("1000000000000", memodb_value(1000000000000));
  test_print("-1", memodb_value(-1));
  test_print("-1000000000000", memodb_value(-1000000000000));
}

TEST(ValuePrintTest, Float) {
  test_print("1.5", memodb_value(1.5));
  test_print("-4.5", memodb_value(-4.5));
  test_print("Infinity", memodb_value(INFINITY));
  test_print("-Infinity", memodb_value(-INFINITY));
  test_print("NaN", memodb_value(NAN));
}

TEST(ValuePrintTest, Bool) {
  test_print("true", memodb_value(true));
  test_print("false", memodb_value(false));
}

TEST(ValuePrintTest, Null) { test_print("null", memodb_value(nullptr)); }

TEST(ValuePrintTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_print("''", memodb_value(bytes{}));
  test_print("'ascii'", memodb_value(bytes{0x61, 0x73, 0x63, 0x69, 0x69}));
  test_print("h'00'", memodb_value(bytes{0x00}));
  test_print("'\"'", memodb_value(bytes{0x22}));
  test_print("'\\''", memodb_value(bytes{0x27}));
  test_print("'\\\\'", memodb_value(bytes{0x5c}));
  test_print("h'7f'", memodb_value(bytes{0x7f}));
  test_print("h'80'", memodb_value(bytes{0x80}));
  test_print("h'00ff30'", memodb_value(bytes{0x00, 0xff, 0x30}));
}

TEST(ValuePrintTest, String) {
  test_print("\"\"", memodb_value(""));
  test_print("\"foo bar\"", memodb_value("foo bar"));
  test_print("\"\\\"\"", memodb_value("\""));
  test_print("\"\\\\\"", memodb_value("\\"));
  test_print("\"\\u0000\\n\"",
             memodb_value::string(llvm::StringRef("\x00\n", 2)));
  test_print("\"\\u0001\\u007f\"",
             memodb_value::string(llvm::StringRef("\x01\x7f", 2)));
  test_print(
      "\"\xe2\x80\xa2\xf0\x9d\x84\x9e\"",
      memodb_value::string(llvm::StringRef("\xe2\x80\xa2\xf0\x9d\x84\x9e", 7)));
}

TEST(ValuePrintTest, Array) {
  test_print("[]", memodb_value::array());
  test_print("[1]", memodb_value::array({1}));
  test_print("[1, 2]", memodb_value::array({1, 2}));
}

TEST(ValuePrintTest, Map) {
  test_print("{}", memodb_value::map());
  test_print("{\"x\": 1, \"y\": 2}", memodb_value::map({{"x", 1}, {"y", 2}}));
}

TEST(ValuePrintTest, Ref) {
  test_print("42(h'0001710001f6')",
             memodb_value(CID::fromCID({0x01, 0x71, 0x00, 0x01, 0xf6})));
  test_print(
      "42(h'"
      "000171a0e4022003170a2e7597b7b7e3d84c05391d139a62b157e78786d8c082f29dcf4c"
      "111314')",
      memodb_value(CID::fromBlake2BMerkleDAG(
          {0x03, 0x17, 0x0a, 0x2e, 0x75, 0x97, 0xb7, 0xb7, 0xe3, 0xd8, 0x4c,
           0x05, 0x39, 0x1d, 0x13, 0x9a, 0x62, 0xb1, 0x57, 0xe7, 0x87, 0x86,
           0xd8, 0xc0, 0x82, 0xf2, 0x9d, 0xcf, 0x4c, 0x11, 0x13, 0x14})));
}

} // end anonymous namespace
