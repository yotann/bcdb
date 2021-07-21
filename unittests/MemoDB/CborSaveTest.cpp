#include "memodb/Node.h"

#include "gtest/gtest.h"

using namespace memodb;

namespace {

void test_save(const Node &value, const std::vector<uint8_t> &expected) {
  std::vector<uint8_t> out;
  value.save_cbor(out);
  EXPECT_EQ(expected, out);
}

TEST(CborSaveTest, Integer) {
  test_save(Node(0), {0x00});
  test_save(Node(1), {0x01});
  test_save(Node(10), {0x0a});
  test_save(Node(23), {0x17});
  test_save(Node(24), {0x18, 0x18});
  test_save(Node(25), {0x18, 0x19});
  test_save(Node(100), {0x18, 0x64});
  test_save(Node(1000), {0x19, 0x03, 0xe8});
  test_save(Node(1000000), {0x1a, 0x00, 0x0f, 0x42, 0x40});
  test_save(Node(1000000000000),
            {0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00});
  test_save(Node(-1), {0x20});
  test_save(Node(-10), {0x29});
  test_save(Node(-100), {0x38, 0x63});
  test_save(Node(-1000), {0x39, 0x03, 0xe7});
}

TEST(CborSaveTest, Float) {
  auto check = [](double value, const std::vector<uint8_t> &expected) {
    std::vector<uint8_t> out;
    Node(value).save_cbor(out);
    EXPECT_EQ(expected, out);
  };
  check(0.0, {0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(-0.0, {0xfb, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(1.0, {0xfb, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(1.1, {0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a});
  check(1.5, {0xfb, 0x3f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(65504.0, {0xfb, 0x40, 0xef, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(100000.0, {0xfb, 0x40, 0xf8, 0x6a, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(3.4028234663852886e+38,
        {0xfb, 0x47, 0xef, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00});
  check(1.0e+300, {0xfb, 0x7e, 0x37, 0xe4, 0x3c, 0x88, 0x00, 0x75, 0x9c});
  check(1.7976931348623157e+308,
        {0xfb, 0x7f, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  check(5.960464477539063e-8,
        {0xfb, 0x3e, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(2.2250738585072014e-308,
        {0xfb, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(5e-324, {0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01});
  check(0.00006103515625,
        {0xfb, 0x3f, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(-4.0, {0xfb, 0xc0, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(-4.1, {0xfb, 0xc0, 0x10, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66});
  check(INFINITY, {0xfb, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(NAN, {0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  check(-INFINITY, {0xfb, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

TEST(CborSaveTest, Bool) {
  test_save(Node(false), {0xf4});
  test_save(Node(true), {0xf5});
}

TEST(CborSaveTest, Null) { test_save(Node{nullptr}, {0xf6}); }

TEST(CborSaveTest, Bytes) {
  using bytes = std::vector<uint8_t>;
  test_save(Node(bytes{}), {0x40});
  test_save(Node(bytes{0x01, 0x02, 0x03, 0x04}),
            {0x44, 0x01, 0x02, 0x03, 0x04});
}

TEST(CborSaveTest, String) {
  test_save(Node(""), {0x60});
  test_save(Node("a"), {0x61, 0x61});
  test_save(Node("IETF"), {0x64, 0x49, 0x45, 0x54, 0x46});
  test_save(Node("\"\\"), {0x62, 0x22, 0x5c});
  test_save(Node("\u00fc"), {0x62, 0xc3, 0xbc});
  test_save(Node("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_save(Node("\u6c34"), {0x63, 0xe6, 0xb0, 0xb4});
  test_save(Node("\U00010151"), {0x64, 0xf0, 0x90, 0x85, 0x91});
}

TEST(CborSaveTest, List) {
  test_save(Node(node_list_arg), {0x80});
  test_save(Node(node_list_arg, {1, 2, 3}), {0x83, 0x01, 0x02, 0x03});
  test_save(Node(node_list_arg,
                 {1, Node(node_list_arg, {2, 3}), Node(node_list_arg, {4, 5})}),
            {0x83, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05});
  test_save(
      Node(node_list_arg, {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                           14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}),
      {0x98, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
       0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
       0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19});
}

TEST(CborSaveTest, Map) {
  test_save(Node(node_map_arg), {0xa0});
  test_save(Node(node_map_arg,
                 {{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}}),
            {0xa5, 0x61, 0x61, 0x61, 0x41, 0x61, 0x62, 0x61, 0x42, 0x61, 0x63,
             0x61, 0x43, 0x61, 0x64, 0x61, 0x44, 0x61, 0x65, 0x61, 0x45});
}

TEST(CborSaveTest, Mixed) {
  test_save(Node(node_list_arg, {"a", Node(node_map_arg, {{"b", "c"}})}),
            {0x82, 0x61, 0x61, 0xa1, 0x61, 0x62, 0x61, 0x63});
  test_save(Node(node_map_arg, {{"a", 1}, {"b", Node(node_list_arg, {2, 3})}}),
            {0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03});
}

TEST(CborSaveTest, Ref) {
  test_save(Node(*CID::fromBytes({0x01, 0x71, 0x00, 0x01, 0xf6})),
            {0xd8, 0x2a, 0x46, 0x00, 0x01, 0x71, 0x00, 0x01, 0xf6});
  test_save(Node(*CID::fromBytes(
                {0x01, 0x71, 0xa0, 0xe4, 0x02, 0x20, 0x03, 0x17, 0x0a, 0x2e,
                 0x75, 0x97, 0xb7, 0xb7, 0xe3, 0xd8, 0x4c, 0x05, 0x39, 0x1d,
                 0x13, 0x9a, 0x62, 0xb1, 0x57, 0xe7, 0x87, 0x86, 0xd8, 0xc0,
                 0x82, 0xf2, 0x9d, 0xcf, 0x4c, 0x11, 0x13, 0x14})),
            {0xd8, 0x2a, 0x58, 0x27, 0x00, 0x01, 0x71, 0xa0, 0xe4, 0x02, 0x20,
             0x03, 0x17, 0x0a, 0x2e, 0x75, 0x97, 0xb7, 0xb7, 0xe3, 0xd8, 0x4c,
             0x05, 0x39, 0x1d, 0x13, 0x9a, 0x62, 0xb1, 0x57, 0xe7, 0x87, 0x86,
             0xd8, 0xc0, 0x82, 0xf2, 0x9d, 0xcf, 0x4c, 0x11, 0x13, 0x14});
}

TEST(CborSaveTest, MapOrdering) {
  test_save(Node(node_map_arg,
                 {
                     {"", {}},
                     {"a", {}},
                     {"m", {}},
                     {"bb", {}},
                     {"nn", {}},
                     {"\xc2\x80", {}},
                     {"ccc", {}},
                     {"ooo", {}},
                 }),
            {
                0xa8, 0x60, 0xf6, 0x61, 0x61, 0xf6, 0x61, 0x6d,
                0xf6, 0x62, 0x62, 0x62, 0xf6, 0x62, 0x6e, 0x6e,
                0xf6, 0x62, 0xc2, 0x80, 0xf6, 0x63, 0x63, 0x63,
                0x63, 0xf6, 0x63, 0x6f, 0x6f, 0x6f, 0xf6,
            });
}

} // end anonymous namespace
