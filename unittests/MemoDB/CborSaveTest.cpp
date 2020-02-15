#include "memodb/memodb.h"

#include "gtest/gtest.h"

namespace {

void test_save(const memodb_value &value,
               const std::vector<uint8_t> &expected) {
  std::vector<uint8_t> out;
  value.save_cbor(out);
  EXPECT_EQ(expected, out);
}

TEST(CborSaveTest, Integer) {
  test_save(memodb_value(0), {0x00});
  test_save(memodb_value(1), {0x01});
  test_save(memodb_value(10), {0x0a});
  test_save(memodb_value(23), {0x17});
  test_save(memodb_value(24), {0x18, 0x18});
  test_save(memodb_value(25), {0x18, 0x19});
  test_save(memodb_value(100), {0x18, 0x64});
  test_save(memodb_value(1000), {0x19, 0x03, 0xe8});
  test_save(memodb_value(1000000), {0x1a, 0x00, 0x0f, 0x42, 0x40});
  test_save(memodb_value(1000000000000),
            {0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00});
  test_save(memodb_value(-1), {0x20});
  test_save(memodb_value(-10), {0x29});
  test_save(memodb_value(-100), {0x38, 0x63});
  test_save(memodb_value(-1000), {0x39, 0x03, 0xe7});
}

TEST(CborSaveTest, Bool) {
  test_save(memodb_value(false), {0xf4});
  test_save(memodb_value(true), {0xf5});
}

TEST(CborSaveTest, Null) { test_save(memodb_value{nullptr}, {0xf6}); }

TEST(CborSaveTest, Undefined) { test_save(memodb_value{}, {0xf7}); }

TEST(CborSaveTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_save(memodb_value(bytes{}), {0x40});
  test_save(memodb_value(bytes{0x01, 0x02, 0x03, 0x04}),
            {0x44, 0x01, 0x02, 0x03, 0x04});
}

TEST(CborSaveTest, String) {
  test_save(memodb_value(""), {0x60});
  test_save(memodb_value("a"), {0x61, 0x61});
  test_save(memodb_value("IETF"), {0x64, 0x49, 0x45, 0x54, 0x46});
  test_save(memodb_value("\"\\"), {0x62, 0x22, 0x5c});
  test_save(memodb_value("\u00fc"), {0x62, 0xc3, 0xbc});
  test_save(memodb_value("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_save(memodb_value("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_save(memodb_value("\U00010151"), {0x64, 0xf0, 0x90, 0x85, 0x91});
}

TEST(CborSaveTest, Array) {
  test_save(memodb_value::array(), {0x80});
  test_save(memodb_value::array({1, 2, 3}), {0x83, 0x01, 0x02, 0x03});
  test_save(memodb_value::array(
                {1, memodb_value::array({2, 3}), memodb_value::array({4, 5})}),
            {0x83, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05});
  test_save(
      memodb_value::array({1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                           14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}),
      {0x98, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
       0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
       0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19});
}

TEST(CborSaveTest, Map) {
  test_save(memodb_value::map(), {0xa0});
  test_save(memodb_value::map({{1, 2}, {3, 4}}),
            {0xa2, 0x01, 0x02, 0x03, 0x04});
  test_save(memodb_value::map(
                {{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}}),
            {0xa5, 0x61, 0x61, 0x61, 0x41, 0x61, 0x62, 0x61, 0x42, 0x61, 0x63,
             0x61, 0x43, 0x61, 0x64, 0x61, 0x44, 0x61, 0x65, 0x61, 0x45});
}

TEST(CborSaveTest, Mixed) {
  test_save(memodb_value::array({"a", memodb_value::map({{"b", "c"}})}),
            {0x82, 0x61, 0x61, 0xa1, 0x61, 0x62, 0x61, 0x63});
  test_save(memodb_value::map({{"a", 1}, {"b", memodb_value::array({2, 3})}}),
            {0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03});
}

TEST(CborSaveTest, Ref) {
  test_save(memodb_ref(""), {0xd8, 0x27, 0x60});
  test_save(memodb_ref("x"), {0xd8, 0x27, 0x61, 0x78});
}

TEST(CborSaveTest, MapOrdering) {
  using bytes = std::vector<uint8_t>;
  // test integers of different lengths
  test_save(memodb_value::map({
                {0, {}},
                {1, {}},
                {24, {}},
                {256, {}},
                {-1, {}},
                {-25, {}},
                {-257, {}},
            }),
            {0xa7, 0x00, 0xf7, 0x01, 0xf7, 0x20, 0xf7, 0x18, 0x18, 0xf7, 0x38,
             0x18, 0xf7, 0x19, 0x01, 0x00, 0xf7, 0x39, 0x01, 0x00, 0xf7});
  // test one-byte values of different types
  test_save(memodb_value::map({
                {{}, {}},
                {nullptr, {}},
                {false, {}},
                {true, {}},
                {0, {}},
                {-1, {}},
                {memodb_value(bytes{}), {}},
                {"", {}},
                {memodb_ref(""), {}},
                {memodb_value::array(), {}},
                {memodb_value::map(), {}},
            }),
            {
                0xab, 0x00, 0xf7, 0x20, 0xf7, 0x40, 0xf7, 0x60, 0xf7,
                0x80, 0xf7, 0xa0, 0xf7, 0xf4, 0xf7, 0xf5, 0xf7, 0xf6,
                0xf7, 0xf7, 0xf7, 0xd8, 0x27, 0x60, 0xf7,
            });
}

} // end anonymous namespace
