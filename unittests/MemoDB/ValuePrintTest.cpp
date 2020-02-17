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

TEST(ValuePrintTest, Undefined) { test_print("undefined", memodb_value{}); }

TEST(ValuePrintTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_print("h''", memodb_value(bytes{}));
  test_print("h'00ff30'", memodb_value(bytes{0x00, 0xff, 0x30}));
}

TEST(ValuePrintTest, String) {
  test_print("\"\"", memodb_value(""));
  test_print("\"foo bar\"", memodb_value("foo bar"));
  test_print("\"\\\"\"", memodb_value("\""));
  test_print("\"\\\\\"", memodb_value("\\"));
  test_print("\"\\u0000\\u000a\"",
             memodb_value::string(llvm::StringRef("\x00\n", 2)));
}

TEST(ValuePrintTest, Array) {
  test_print("[]", memodb_value::array());
  test_print("[1]", memodb_value::array({1}));
  test_print("[1, 2]", memodb_value::array({1, 2}));
}

TEST(ValuePrintTest, Map) {
  test_print("{}", memodb_value::map());
  test_print("{1: false}", memodb_value::map({{1, false}}));
  test_print("{\"x\": 1, \"y\": 2}", memodb_value::map({{"x", 1}, {"y", 2}}));
}

TEST(ValuePrintTest, Ref) {
  test_print("39(\"\")", memodb_value(memodb_ref()));
  test_print("39(\"foo\")", memodb_value(memodb_ref("foo")));
  test_print("39(\"\\\"\\\\\")", memodb_value(memodb_ref("\"\\")));
}

} // end anonymous namespace
