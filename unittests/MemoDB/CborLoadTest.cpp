#include "memodb/memodb.h"

#include "gtest/gtest.h"

namespace {

void test_load(const memodb_value &expected, const std::vector<uint8_t> &cbor) {
  memodb_value actual = memodb_value::load_cbor(cbor);
  EXPECT_EQ(expected, actual);
}

TEST(CborLoadTest, Integer) {
  test_load(memodb_value(0), {0x00});
  test_load(memodb_value(1), {0x01});
  test_load(memodb_value(10), {0x0a});
  test_load(memodb_value(23), {0x17});
  test_load(memodb_value(24), {0x18, 0x18});
  test_load(memodb_value(25), {0x18, 0x19});
  test_load(memodb_value(100), {0x18, 0x64});
  test_load(memodb_value(1000), {0x19, 0x03, 0xe8});
  test_load(memodb_value(1000000), {0x1a, 0x00, 0x0f, 0x42, 0x40});
  test_load(memodb_value(1000000000000),
            {0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00});
  test_load(memodb_value(-1), {0x20});
  test_load(memodb_value(-10), {0x29});
  test_load(memodb_value(-100), {0x38, 0x63});
  test_load(memodb_value(-1000), {0x39, 0x03, 0xe7});

  test_load(memodb_value(0),
            {0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

TEST(CborLoadTest, Float) {
  auto check = [](double expected, const std::vector<uint8_t> &cbor) {
    memodb_value value = memodb_value::load_cbor(cbor);
    ASSERT_EQ(memodb_value::FLOAT, value.type());
    double actual = value.as_float();
    if (std::isnan(expected))
      ASSERT_TRUE(std::isnan(actual));
    else
      ASSERT_EQ(expected, actual);
  };
  check(0.0, {0xf9, 0x00, 0x00});
  check(-0.0, {0xf9, 0x80, 0x00});
  check(1.0, {0xf9, 0x3c, 0x00});
  check(1.1, {0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a});
  check(1.5, {0xf9, 0x3e, 0x00});
  check(65504.0, {0xf9, 0x7b, 0xff});
  check(100000.0, {0xfa, 0x47, 0xc3, 0x50, 0x00});
  check(3.4028234663852886e+38, {0xfa, 0x7f, 0x7f, 0xff, 0xff});
  check(1.0e+300, {0xfb, 0x7e, 0x37, 0xe4, 0x3c, 0x88, 0x00, 0x75, 0x9c});
  check(5.960464477539063e-8, {0xf9, 0x00, 0x01});
  check(0.00006103515625, {0xf9, 0x04, 0x00});
  check(-4.0, {0xf9, 0xc4, 0x00});
  check(-4.1, {0xfb, 0xc0, 0x10, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66});
  check(INFINITY, {0xf9, 0x7c, 0x00});
  check(NAN, {0xf9, 0x7e, 0x00});
  check(-INFINITY, {0xf9, 0xfc, 0x00});
  check(INFINITY, {0xfa, 0x7f, 0x80, 0x00, 0x00});
  check(NAN, {0xfa, 0x7f, 0xc0, 0x00, 0x00});
  check(-INFINITY, {0xfa, 0xff, 0x80, 0x00, 0x00});
  check(INFINITY, {0xfb, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(NAN, {0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(-INFINITY, {0xfb, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

TEST(CborLoadTest, Bool) {
  test_load(memodb_value(false), {0xf4});
  test_load(memodb_value(true), {0xf5});
}

TEST(CborLoadTest, Null) { test_load(memodb_value{nullptr}, {0xf6}); }

TEST(CborLoadTest, Undefined) { test_load(memodb_value{}, {0xf7}); }

TEST(CborLoadTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_load(memodb_value(bytes{}), {0x40});
  test_load(memodb_value(bytes{0x01, 0x02, 0x03, 0x04}),
            {0x44, 0x01, 0x02, 0x03, 0x04});
  test_load(memodb_value(bytes{0x01, 0x02, 0x03, 0x04, 0x05}),
            {0x5f, 0x42, 0x01, 0x02, 0x43, 0x03, 0x04, 0x05, 0xff});
}

TEST(CborLoadTest, String) {
  test_load(memodb_value(""), {0x60});
  test_load(memodb_value("a"), {0x61, 0x61});
  test_load(memodb_value("IETF"), {0x64, 0x49, 0x45, 0x54, 0x46});
  test_load(memodb_value("\"\\"), {0x62, 0x22, 0x5c});
  test_load(memodb_value("\u00fc"), {0x62, 0xc3, 0xbc});
  test_load(memodb_value("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_load(memodb_value("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_load(memodb_value("\U00010151"), {0x64, 0xf0, 0x90, 0x85, 0x91});
  test_load(memodb_value("streaming"),
            {0x7f, 0x65, 0x73, 0x74, 0x72, 0x65, 0x61, 0x64, 0x6d, 0x69, 0x6e,
             0x67, 0xff});
}

TEST(CborLoadTest, Array) {
  test_load(memodb_value::array(), {0x80});
  test_load(memodb_value::array({1, 2, 3}), {0x83, 0x01, 0x02, 0x03});
  test_load(memodb_value::array(
                {1, memodb_value::array({2, 3}), memodb_value::array({4, 5})}),
            {0x83, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05});
  test_load(
      memodb_value::array({1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                           14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}),
      {0x98, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
       0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
       0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19});

  test_load(memodb_value::array(), {0x9f, 0xff});
  test_load(memodb_value::array(
                {1, memodb_value::array({2, 3}), memodb_value::array({4, 5})}),
            {0x9f, 0x01, 0x82, 0x02, 0x03, 0x9f, 0x04, 0x05, 0xff, 0xff});
  test_load(memodb_value::array(
                {1, memodb_value::array({2, 3}), memodb_value::array({4, 5})}),
            {0x9f, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05, 0xff});
  test_load(memodb_value::array(
                {1, memodb_value::array({2, 3}), memodb_value::array({4, 5})}),
            {0x83, 0x01, 0x82, 0x02, 0x03, 0x9f, 0x04, 0x05, 0xff});
  test_load(memodb_value::array(
                {1, memodb_value::array({2, 3}), memodb_value::array({4, 5})}),
            {0x83, 0x01, 0x9f, 0x02, 0x03, 0xff, 0x82, 0x04, 0x05});
  test_load(
      memodb_value::array({1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                           14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}),
      {0x9f, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
       0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
       0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19, 0xff});
}

TEST(CborLoadTest, Map) {
  test_load(memodb_value::map(), {0xa0});
  test_load(memodb_value::map({{1, 2}, {3, 4}}),
            {0xa2, 0x01, 0x02, 0x03, 0x04});
  test_load(memodb_value::map(
                {{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}}),
            {0xa5, 0x61, 0x61, 0x61, 0x41, 0x61, 0x62, 0x61, 0x42, 0x61, 0x63,
             0x61, 0x43, 0x61, 0x64, 0x61, 0x44, 0x61, 0x65, 0x61, 0x45});
  test_load(
      memodb_value::map({{"Fun", true}, {"Amt", -2}}),
      {0xbf, 0x63, 0x46, 0x75, 0x6e, 0xf5, 0x63, 0x41, 0x6d, 0x74, 0x21, 0xff});
}

TEST(CborLoadTest, Mixed) {
  test_load(memodb_value::array({"a", memodb_value::map({{"b", "c"}})}),
            {0x82, 0x61, 0x61, 0xa1, 0x61, 0x62, 0x61, 0x63});
  test_load(memodb_value::map({{"a", 1}, {"b", memodb_value::array({2, 3})}}),
            {0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03});
  test_load(memodb_value::map({{"a", 1}, {"b", memodb_value::array({2, 3})}}),
            {0xbf, 0x61, 0x61, 0x01, 0x61, 0x62, 0x9f, 0x02, 0x03, 0xff, 0xff});
  test_load(memodb_value::array({"a", memodb_value::map({{"b", "c"}})}),
            {0x82, 0x61, 0x61, 0xbf, 0x61, 0x62, 0x61, 0x63, 0xff});
}

TEST(CborLoadTest, Ref) {
  test_load(memodb_ref(""), {0xd8, 0x27, 0x60});
  test_load(memodb_ref("x"), {0xd8, 0x27, 0x61, 0x78});
}

} // end anonymous namespace
