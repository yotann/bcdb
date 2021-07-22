#include "memodb/Node.h"

#include "gtest/gtest.h"
#include <sstream>

using namespace memodb;

namespace {

void test_print(llvm::StringRef expected, const Node &value) {
  std::stringstream out;
  out << value;
  EXPECT_EQ(expected, out.str());
}

TEST(JSONWriteTest, Integer) {
  test_print("0", Node(0));
  test_print("1", Node(1));
  test_print("1000000000000", Node(1000000000000));
  test_print("9223372036854775807", Node(9223372036854775807));
  test_print("-1", Node(-1));
  test_print("-1000000000000", Node(-1000000000000));
  test_print("-9223372036854775808", Node(-9223372036854775807 - 1));
}

TEST(JSONWriteTest, Float) {
  test_print("{\"float\":1}", Node(1.0));
  test_print("{\"float\":1.5}", Node(1.5));
  test_print("{\"float\":-4.5}", Node(-4.5));
}

TEST(JSONWriteTest, Bool) {
  test_print("true", Node(true));
  test_print("false", Node(false));
}

TEST(JSONWriteTest, Null) { test_print("null", Node(nullptr)); }

TEST(JSONWriteTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_print("{\"base64\":\"\"}", Node(bytes{}));
  test_print("{\"base64\":\"YXNjaWk=\"}",
             Node(bytes{0x61, 0x73, 0x63, 0x69, 0x69}));
  test_print("{\"base64\":\"AA==\"}", Node(bytes{0x00}));
  test_print("{\"base64\":\"Ig==\"}", Node(bytes{0x22}));
  test_print("{\"base64\":\"Jw==\"}", Node(bytes{0x27}));
  test_print("{\"base64\":\"XA==\"}", Node(bytes{0x5c}));
  test_print("{\"base64\":\"fw==\"}", Node(bytes{0x7f}));
  test_print("{\"base64\":\"gA==\"}", Node(bytes{0x80}));
  test_print("{\"base64\":\"AP8w\"}", Node(bytes{0x00, 0xff, 0x30}));
}

TEST(JSONWriteTest, String) {
  test_print("\"\"", Node(""));
  test_print("\"foo bar\"", Node("foo bar"));
  test_print("\"\\\"\"", Node("\""));
  test_print("\"\\\\\"", Node("\\"));
  test_print("\"\\u0000\\n\"",
             Node(utf8_string_arg, llvm::StringRef("\x00\n", 2)));
  test_print("\"\\u0001\x7f\"",
             Node(utf8_string_arg, llvm::StringRef("\x01\x7f", 2)));
  test_print("\"\xe2\x80\xa2\xf0\x9d\x84\x9e\"",
             Node(utf8_string_arg,
                  llvm::StringRef("\xe2\x80\xa2\xf0\x9d\x84\x9e", 7)));
}

TEST(JSONWriteTest, Array) {
  test_print("[]", Node(node_list_arg));
  test_print("[1]", Node(node_list_arg, {1}));
  test_print("[1,2]", Node(node_list_arg, {1, 2}));
}

TEST(JSONWriteTest, Map) {
  test_print("{\"map\":{}}", Node::Map());
  test_print("{\"map\":{\"x\":1,\"y\":2}}", Node::Map({{"x", 1}, {"y", 2}}));
}

TEST(JSONWriteTest, Link) {
  test_print("{\"cid\":\"uAXEAAfY\"}",
             Node(*CID::fromBytes({0x01, 0x71, 0x00, 0x01, 0xf6})));
  test_print(
      "{\"cid\":\"uAXGg5AIgAxcKLnWXt7fj2EwFOR0TmmKxV-eHhtjAgvKdz0wRExQ\"}",
      Node(*CID::fromBytes({0x01, 0x71, 0xa0, 0xe4, 0x02, 0x20, 0x03, 0x17,
                            0x0a, 0x2e, 0x75, 0x97, 0xb7, 0xb7, 0xe3, 0xd8,
                            0x4c, 0x05, 0x39, 0x1d, 0x13, 0x9a, 0x62, 0xb1,
                            0x57, 0xe7, 0x87, 0x86, 0xd8, 0xc0, 0x82, 0xf2,
                            0x9d, 0xcf, 0x4c, 0x11, 0x13, 0x14})));
}

} // end anonymous namespace
