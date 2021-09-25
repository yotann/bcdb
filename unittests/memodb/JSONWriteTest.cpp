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
  test_print("18446744073709551615",
             Node(static_cast<uint64_t>(18446744073709551615ull)));
  test_print("-1", Node(-1));
  test_print("-1000000000000", Node(-1000000000000));
  test_print("-9223372036854775808", Node(-9223372036854775807 - 1));
}

TEST(JSONWriteTest, Float) {
  // RFC8785 Appendix B
  test_print("{\"float\":\"0\"}", Node(0.0));
  test_print("{\"float\":\"-0\"}", Node(-0.0));
  test_print("{\"float\":\"5e-324\"}", Node(0x0.0000000000001p-1022));
  test_print("{\"float\":\"-5e-324\"}", Node(-0x0.0000000000001p-1022));
  test_print("{\"float\":\"1.7976931348623157e+308\"}",
             Node(0x1.fffffffffffffp+1023));
  test_print("{\"float\":\"-1.7976931348623157e+308\"}",
             Node(-0x1.fffffffffffffp+1023));
  test_print("{\"float\":\"9007199254740992\"}", Node(0x1.0p+53));
  test_print("{\"float\":\"-9007199254740992\"}", Node(-0x1.0p+53));
  test_print("{\"float\":\"295147905179352830000\"}", Node(0x1.0p+68));
  test_print("{\"float\":\"NaN\"}", Node(NAN));
  test_print("{\"float\":\"Infinity\"}", Node(INFINITY));
  test_print("{\"float\":\"9.999999999999997e+22\"}",
             Node(0x1.52d02c7e14af5p+76));
  test_print("{\"float\":\"1e+23\"}", Node(0x1.52d02c7e14af6p+76));
  test_print("{\"float\":\"1.0000000000000001e+23\"}",
             Node(0x1.52d02c7e14af7p+76));
  test_print("{\"float\":\"999999999999999700000\"}",
             Node(0x1.b1ae4d6e2ef4ep+69));
  test_print("{\"float\":\"999999999999999900000\"}",
             Node(0x1.b1ae4d6e2ef4fp+69));
  test_print("{\"float\":\"1e+21\"}", Node(0x1.b1ae4d6e2ef50p+69));
  test_print("{\"float\":\"9.999999999999997e-7\"}",
             Node(0x1.0c6f7a0b5ed8cp-20));
  test_print("{\"float\":\"0.000001\"}", Node(0x1.0c6f7a0b5ed8dp-20));
  test_print("{\"float\":\"333333333.3333332\"}", Node(0x1.3de4355555553p+28));
  test_print("{\"float\":\"333333333.33333325\"}", Node(0x1.3de4355555554p+28));
  test_print("{\"float\":\"333333333.3333333\"}", Node(0x1.3de4355555555p+28));
  test_print("{\"float\":\"333333333.3333334\"}", Node(0x1.3de4355555556p+28));
  test_print("{\"float\":\"333333333.33333343\"}", Node(0x1.3de4355555557p+28));
  test_print("{\"float\":\"-0.0000033333333333333333\"}",
             Node(-0x1.bf647612f3696p-19));
  test_print("{\"float\":\"1424953923781206.2\"}", Node(0x1.43ff3c1cb0959p+50));

  // other tests
  test_print("{\"float\":\"-Infinity\"}", Node(-INFINITY));
  test_print("{\"float\":\"1\"}", Node(1.0));
  test_print("{\"float\":\"-1\"}", Node(-1.0));
  test_print("{\"float\":\"1.5\"}", Node(1.5));
  test_print("{\"float\":\"-4.5\"}", Node(-4.5));
  test_print("{\"float\":\"3.141592653589793\"}", Node(0x1.921fb54442d18p+1));
  test_print("{\"float\":\"-123456.78\"}", Node(-0x1.e240c7ae147aep+16));
  test_print("{\"float\":\"123456.78\"}", Node(0x1.e240c7ae147aep+16));
  test_print("{\"float\":\"100000000000000000000\"}",
             Node(0x1.5af1d78b58c4p+66));
  test_print("{\"float\":\"0.1\"}", Node(0x1.999999999999ap-4));
  test_print("{\"float\":\"1e-7\"}", Node(0x1.ad7f29abcaf48p-24));
  test_print("{\"float\":\"0.0000011\"}", Node(0x1.27476ca61b882p-20));
  test_print("{\"float\":\"1.1e-7\"}", Node(0x1.d87247702c0dp-24));
  test_print("{\"float\":\"100000000001\"}", Node(0x1.74876e801p+36));
  test_print("{\"float\":\"10000000000.1\"}", Node(0x1.2a05f2000cccdp+33));
  test_print("{\"float\":\"-1.000000000000001e-308\"}",
             Node(-0x0.730d67819e8d4p-1022));
  test_print("{\"float\":\"-1.0000000000000004e-308\"}",
             Node(-0x0.730d67819e8d3p-1022));
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
  test_print("\"\\u0007\\b\\t\\n\\u000b\\f\\r\\u000e\"",
             Node("\x07\x08\x09\x0a\x0b\x0c\x0d\x0e"));
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
