#include "memodb/Node.h"

#include <cmath>
#include <sstream>

#include "memodb/Store.h"
#include "gtest/gtest.h"

using namespace memodb;

// https://github.com/nst/JSONTestSuite/tree/master/test_parsing

namespace {

void test_load(llvm::StringRef json, const Node &expected) {
  // FIXME: use mock store
  auto store = Store::open("sqlite:test?mode=memory", true);
  auto actualOrErr = Node::loadFromJSON(*store, json);
  EXPECT_EQ(true, (bool)actualOrErr) << json.str();
  EXPECT_EQ(expected, *actualOrErr);
}

void test_invalid(llvm::StringRef json) {
  // FIXME: use mock store
  auto store = Store::open("sqlite:test?mode=memory", true);
  auto actualOrErr = Node::loadFromJSON(*store, json);
  ASSERT_FALSE(static_cast<bool>(actualOrErr)) << json.str();
  llvm::consumeError(actualOrErr.takeError());
}

TEST(JSONLoadTest, Integer) {
  test_load("0", Node(0));
  test_load("-0", Node(0));
  test_load("1", Node(1));
  test_load("1000000000000", Node(1000000000000));
  test_load("9223372036854775807", Node(9223372036854775807));
  test_load("18446744073709551615",
            Node(static_cast<uint64_t>(18446744073709551615ull)));
  test_load("-1", Node(-1));
  test_load("-1000000000000", Node(-1000000000000));
  test_load("-9223372036854775808", Node(-9223372036854775807 - 1));
}

TEST(JSONLoadTest, Float) {
  // FIXME: use mock store
  auto store = Store::open("sqlite:test?mode=memory", true);
  // RFC8785 Appendix B
  test_load("{\"float\":\"0\"}", Node(0.0));
  auto actualOrErr = Node::loadFromJSON(*store, "{\"float\":\"-0\"}");
  EXPECT_EQ(true, (bool)actualOrErr);
  EXPECT_EQ(Node(0.0), *actualOrErr);
  EXPECT_TRUE(std::signbit(actualOrErr->as<double>()));
  test_load("{\"float\":\"5e-324\"}", Node(0x0.0000000000001p-1022));
  test_load("{\"float\":\"-5e-324\"}", Node(-0x0.0000000000001p-1022));
  test_load("{\"float\":\"1.7976931348623157e+308\"}",
            Node(0x1.fffffffffffffp+1023));
  test_load("{\"float\":\"-1.7976931348623157e+308\"}",
            Node(-0x1.fffffffffffffp+1023));
  test_load("{\"float\":\"9007199254740992\"}", Node(0x1.0p+53));
  test_load("{\"float\":\"-9007199254740992\"}", Node(-0x1.0p+53));
  test_load("{\"float\":\"295147905179352830000\"}", Node(0x1.0p+68));
  actualOrErr = Node::loadFromJSON(*store, "{\"float\":\"NaN\"}");
  EXPECT_EQ(true, (bool)actualOrErr);
  EXPECT_TRUE(std::isnan(actualOrErr->as<double>()));
  test_load("{\"float\":\"Infinity\"}", Node(INFINITY));
  test_load("{\"float\":\"9.999999999999997e+22\"}",
            Node(0x1.52d02c7e14af5p+76));
  test_load("{\"float\":\"1e+23\"}", Node(0x1.52d02c7e14af6p+76));
  test_load("{\"float\":\"1.0000000000000001e+23\"}",
            Node(0x1.52d02c7e14af7p+76));
  test_load("{\"float\":\"999999999999999700000\"}",
            Node(0x1.b1ae4d6e2ef4ep+69));
  test_load("{\"float\":\"999999999999999900000\"}",
            Node(0x1.b1ae4d6e2ef4fp+69));
  test_load("{\"float\":\"1e+21\"}", Node(0x1.b1ae4d6e2ef50p+69));
  test_load("{\"float\":\"9.999999999999997e-7\"}",
            Node(0x1.0c6f7a0b5ed8cp-20));
  test_load("{\"float\":\"0.000001\"}", Node(0x1.0c6f7a0b5ed8dp-20));
  test_load("{\"float\":\"333333333.3333332\"}", Node(0x1.3de4355555553p+28));
  test_load("{\"float\":\"333333333.33333325\"}", Node(0x1.3de4355555554p+28));
  test_load("{\"float\":\"333333333.3333333\"}", Node(0x1.3de4355555555p+28));
  test_load("{\"float\":\"333333333.3333334\"}", Node(0x1.3de4355555556p+28));
  test_load("{\"float\":\"333333333.33333343\"}", Node(0x1.3de4355555557p+28));
  test_load("{\"float\":\"-0.0000033333333333333333\"}",
            Node(-0x1.bf647612f3696p-19));
  test_load("{\"float\":\"1424953923781206.2\"}", Node(0x1.43ff3c1cb0959p+50));

  // other tests
  test_load("{\"float\":\"-Infinity\"}", Node(-INFINITY));
  test_load("{\"float\":\"1\"}", Node(1.0));
  test_load("{\"float\":\"-1\"}", Node(-1.0));
  test_load("{\"float\":\"1.5\"}", Node(1.5));
  test_load("{\"float\":\"-4.5\"}", Node(-4.5));
  test_load("{\"float\":\"3.141592653589793\"}", Node(0x1.921fb54442d18p+1));
  test_load("{\"float\":\"-123456.78\"}", Node(-0x1.e240c7ae147aep+16));
  test_load("{\"float\":\"123456.78\"}", Node(0x1.e240c7ae147aep+16));
  test_load("{\"float\":\"100000000000000000000\"}",
            Node(0x1.5af1d78b58c4p+66));
  test_load("{\"float\":\"100000000000000000000000000000000000000001\"}",
            Node(0x1.25dfa371a19e7p+136));
  test_load("{\"float\":\"0.1\"}", Node(0x1.999999999999ap-4));
  test_load("{\"float\":\"0.00000000000000000000000000000000000000001\"}",
            Node(0x1.be03d0bf225c7p-137));
  test_load("{\"float\":\"1e-7\"}", Node(0x1.ad7f29abcaf48p-24));
  test_load("{\"float\":\"0.0000011\"}", Node(0x1.27476ca61b882p-20));
  test_load("{\"float\":\"1.1e-7\"}", Node(0x1.d87247702c0dp-24));
  test_load("{\"float\":\"100000000001\"}", Node(0x1.74876e801p+36));
  test_load("{\"float\":\"10000000000.1\"}", Node(0x1.2a05f2000cccdp+33));
  test_load("{\"float\":\"-1.000000000000001e-308\"}",
            Node(-0x0.730d67819e8d4p-1022));
  test_load("{\"float\":\"-1.0000000000000004e-308\"}",
            Node(-0x0.730d67819e8d3p-1022));
  test_load(
      "{\"float\":\"-1."
      "00000000000000065042509409911827826032367803636410424129692898e-308\"}",
      Node(-1.000000000000001e-308));
  test_load(
      "{\"float\":\"-1."
      "00000000000000065042509409911827826032367803636410424129692897e-308\"}",
      Node(-1.0000000000000004e-308));
  test_load("{\"float\":\"123.456e-789\"}", Node(0.0));
  test_load("{\"float\":\"1.5e+9999\"}", Node(INFINITY));
  test_load("{\"float\":\"0e+1\"}", Node(0.0));
  test_load("{\"float\":\"0e1\"}", Node(0.0));
  test_load("{\"float\":\"1E22\"}", Node(1e22));
}

TEST(JSONLoadTest, Bool) {
  test_load("true", Node(true));
  test_load("false", Node(false));
}

TEST(JSONLoadTest, Null) { test_load("null", Node(nullptr)); }

TEST(JSONLoadTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_load("{\"base64\":\"\"}", Node(bytes{}));
  test_load("{\"base64\":\"YXNjaWk=\"}",
            Node(bytes{0x61, 0x73, 0x63, 0x69, 0x69}));
  test_load("{\"base64\":\"AA==\"}", Node(bytes{0x00}));
  test_load("{\"base64\":\"Ig==\"}", Node(bytes{0x22}));
  test_load("{\"base64\":\"Jw==\"}", Node(bytes{0x27}));
  test_load("{\"base64\":\"XA==\"}", Node(bytes{0x5c}));
  test_load("{\"base64\":\"fw==\"}", Node(bytes{0x7f}));
  test_load("{\"base64\":\"gA==\"}", Node(bytes{0x80}));
  test_load("{\"base64\":\"AP8w\"}", Node(bytes{0x00, 0xff, 0x30}));
}

TEST(JSONLoadTest, String) {
  test_load("\"\"", Node(""));
  test_load("\"foo bar\"", Node("foo bar"));
  test_load("\"\\\"\"", Node("\""));
  test_load("\"\\\\\"", Node("\\"));
  test_load("\"\\u0000\\n\"",
            Node(utf8_string_arg, llvm::StringRef("\x00\n", 2)));
  test_load("\"\\u0001\x7f\"",
            Node(utf8_string_arg, llvm::StringRef("\x01\x7f", 2)));
  test_load("\"\xe2\x80\xa2\xf0\x9d\x84\x9e\"",
            Node(utf8_string_arg,
                 llvm::StringRef("\xe2\x80\xa2\xf0\x9d\x84\x9e", 7)));
  test_load("\"\\u2022\\ud834\\udd1e\"",
            Node(utf8_string_arg,
                 llvm::StringRef("\xe2\x80\xa2\xf0\x9d\x84\x9e", 7)));
  test_load("\" \"", Node(" ")); // y_string_u+2028_line_sep
  test_load("\" \"", Node(" ")); // y_string_u+2029_par_sep

  test_load("\"\\uDADA\"",
            Node("\ufffd")); // i_string_1st_surrogate_but_2nd_missing
  test_load("\"\\uD888\\u1234\"",
            Node("\ufffd\u1234")); // i_string_1st_valid_surrogate_2nd_invalid
  test_load("\"\\uD800\\n\"",
            Node("\ufffd\n")); // i_string_incomplete_surrogate_and_escape_valid
  test_load("\"\\uDd1ea\"",
            Node("\ufffda")); // i_string_incomplete_surrogate_pair
}

TEST(JSONLoadTest, Array) {
  test_load("[]", Node(node_list_arg));
  test_load("[1]", Node(node_list_arg, {1}));
  test_load("[1,2]", Node(node_list_arg, {1, 2}));
  test_load("[[]   ]", Node(node_list_arg,
                            {Node(node_list_arg)})); // y_array_arraysWithSpaces
  test_load("[1\n]", Node(node_list_arg, {1})); // y_array_with_1_and_newline
  test_load("   [1]", Node(node_list_arg, {1})); // y_array_with_leading_space
  test_load("[2] ", Node(node_list_arg, {2}));   // y_array_with_trailing_space
}

TEST(JSONLoadTest, Map) {
  test_load("{\"map\":{}}", Node::Map());
  test_load("{\"map\":{\"x\":1,\"y\":2}}", Node::Map({{"x", 1}, {"y", 2}}));
  test_load("{\n\"map\"\n:\n{\n\"a\"\n: \"b\"\n}\n}", Node::Map({{"a", "b"}}));
  test_load("{\"map\":{\"x\":1,\"x\\u0000\":1,\"x\\u0000y\":1}}",
            Node::Map({{"x", 1},
                       {llvm::StringRef("x", 2), 1},
                       {llvm::StringRef("x\x00y", 3), 1}}));
  test_load("{\"map\":{\"asd\":\"sdf\", \"dfg\":\"fgh\"}}",
            Node::Map({{"asd", "sdf"}, {"dfg", "fgh"}}));
  test_load("{\"map\":{ \"min\": {\"float\":\"-1.0e+28\"}, \"max\": "
            "{\"float\":\"1.0e+28\"} } }",
            Node::Map({{"min", -1e28}, {"max", 1e28}}));
}

TEST(JSONLoadTest, Link) {
  // FIXME: use a mock store.
  auto store = Store::open("sqlite:test?mode=memory", true);
  test_load("{\"cid\":\"uAXEAAfY\"}",
            Node(*store, *CID::fromBytes({0x01, 0x71, 0x00, 0x01, 0xf6})));
  test_load(
      "{\"cid\":\"uAXGg5AIgAxcKLnWXt7fj2EwFOR0TmmKxV-eHhtjAgvKdz0wRExQ\"}",
      Node(*store,
           *CID::fromBytes({0x01, 0x71, 0xa0, 0xe4, 0x02, 0x20, 0x03, 0x17,
                            0x0a, 0x2e, 0x75, 0x97, 0xb7, 0xb7, 0xe3, 0xd8,
                            0x4c, 0x05, 0x39, 0x1d, 0x13, 0x9a, 0x62, 0xb1,
                            0x57, 0xe7, 0x87, 0x86, 0xd8, 0xc0, 0x82, 0xf2,
                            0x9d, 0xcf, 0x4c, 0x11, 0x13, 0x14})));
}

TEST(JSONLoadTest, InvalidInteger) {
  test_invalid("01");
  test_invalid("-01");
  test_invalid("-123123123123123123123123123123");
  test_invalid("100000000000000000000");
  test_invalid("[++1234]"); // n_number_++
  test_invalid("[+1]");     // n_number_+1
}

TEST(JSONLoadTest, InvalidString) {
  test_invalid("\xed\xa0\x80");             // i_string_UTF8_surrogate_U+D800
  test_invalid("\xe6\x97\xa5\xd1\x88\xfa"); // i_string_UTF-8_invalid_sequence
  test_invalid("\"\x81\""); // i_string_lone_utf8_continuation_byte
}

TEST(JSONLoadTest, InvalidArray) {
  test_invalid("[1 true]");    // n_array_1_true_without_comma
  test_invalid("[\"\": 1]");   // n_array_colon_instead_of_comma
  test_invalid("[\"\"],");     // n_array_comma_after_close
  test_invalid("[,1]");        // n_array_comma_and_number
  test_invalid("[1,,2]");      // n_array_double_comma
  test_invalid("[\"x\"]]");    // n_array_extra_close
  test_invalid("[\"\",]");     // n_array_extra_comma
  test_invalid("[\"x\"");      // n_array_incomplete
  test_invalid("[3[4]]");      // n_array_inner_array_no_comma
  test_invalid("[,]");         // n_array_just_comma
  test_invalid("[   , \"\"]"); // n_array_missing_value
  test_invalid("[1,]");        // n_array_number_and_comma
}

TEST(JSONLoadTest, InvalidBool) {
  test_invalid("[tru]");  // n_incomplete_true
  test_invalid("[nul]");  // n_incomplete_null
  test_invalid("[fals]"); // n_incomplete_false
}

TEST(JSONLoadTest, InvalidCharacter) {
  test_invalid(llvm::StringRef("123", 4)); // n_multidigit_number_then_00
}

TEST(JSONLoadTest, InvalidObject) {
  test_invalid("{\"map\":{\"a\":\"b\",\"a\":\"b\"}}");
  test_invalid("{\"map\":{\"a\":\"b\",\"a\":\"c\"}}");
}

} // end anonymous namespace
