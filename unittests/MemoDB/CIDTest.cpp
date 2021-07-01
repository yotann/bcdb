#include "memodb/CID.h"

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <optional>

#include "memodb/Multibase.h"
#include "gtest/gtest.h"

using namespace memodb;

namespace {

CID identityCID(llvm::ArrayRef<std::uint8_t> Bytes) {
  return CID::calculate(Multicodec::Raw, Bytes, Multicodec::Identity);
}

TEST(CIDTest, Calculate) {
  EXPECT_EQ(CID::fromBytes({0x00, 0x01, 0x71, 0x00, 0x01, 0xf6}),
            CID::calculate(Multicodec::DAG_CBOR, {0xf6}, Multicodec::Identity));
  EXPECT_EQ(CID::fromBytes({0x01, 0x71, 0x00, 0x01, 0xf6}),
            CID::calculate(Multicodec::DAG_CBOR, {0xf6}, Multicodec::Identity));
  EXPECT_EQ(
      CID::fromBytes({0x01, 0x71, 0xa0, 0xe4, 0x02, 0x20, 0x01, 0xf4,
                      0xb7, 0x88, 0x59, 0x3d, 0x4f, 0x70, 0xde, 0x2a,
                      0x45, 0xc2, 0xe1, 0xe8, 0x70, 0x88, 0xbf, 0xbd,
                      0xfa, 0x29, 0x57, 0x7a, 0xe1, 0xb6, 0x2a, 0xba,
                      0x60, 0xe0, 0x95, 0xe3, 0xab, 0x53}),
      CID::calculate(Multicodec::DAG_CBOR, {0xf6}, Multicodec::Blake2b_256));
  EXPECT_EQ(CID::fromBytes({0x01, 0x55, 0xa0, 0xe4, 0x02, 0x20, 0x01, 0xf4,
                            0xb7, 0x88, 0x59, 0x3d, 0x4f, 0x70, 0xde, 0x2a,
                            0x45, 0xc2, 0xe1, 0xe8, 0x70, 0x88, 0xbf, 0xbd,
                            0xfa, 0x29, 0x57, 0x7a, 0xe1, 0xb6, 0x2a, 0xba,
                            0x60, 0xe0, 0x95, 0xe3, 0xab, 0x53}),
            CID::calculate(Multicodec::Raw, {0xf6}, Multicodec::Blake2b_256));
}

TEST(CIDTest, HashSelection) {
  EXPECT_EQ(
      CID::fromBytes({0x01, 0x55, 0x00, 0x20, 0x00, 0x01, 0x02, 0x03, 0x04,
                      0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                      0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                      0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f}),
      CID::calculate(Multicodec::Raw,
                     {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f}));
  EXPECT_EQ(
      CID::fromBytes({0x01, 0x55, 0x00, 0x21, 0x00, 0x01, 0x02, 0x03,
                      0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                      0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
                      0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                      0x1c, 0x1d, 0x1e, 0x1f, 0x20}),
      CID::calculate(Multicodec::Raw,
                     {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
                      0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
                      0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20}));
  EXPECT_EQ(
      CID::fromBytes({0x01, 0x55, 0x00, 0x22, 0x00, 0x01, 0x02, 0x03,
                      0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                      0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
                      0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                      0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21}),
      CID::calculate(Multicodec::Raw,
                     {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
                      0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
                      0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21}));
  EXPECT_EQ(
      CID::fromBytes({0x01, 0x55, 0xa0, 0xe4, 0x02, 0x20, 0x13, 0x89,
                      0x1b, 0x82, 0x3d, 0x3a, 0x2c, 0xfe, 0x0d, 0x1a,
                      0x5e, 0x60, 0xfe, 0x89, 0xd8, 0xc0, 0x91, 0x52,
                      0x4f, 0x99, 0x4c, 0xdc, 0x32, 0x41, 0xc4, 0xda,
                      0x19, 0xc4, 0xbb, 0x3c, 0x2c, 0x6b}),
      CID::calculate(Multicodec::Raw,
                     {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
                      0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
                      0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22}));
}

TEST(CIDTest, FromBytesInvalid) {
  // extra prefix
  EXPECT_EQ(CID::fromBytes({0x00, 0x00, 0x01, 0x71, 0x00, 0x01, 0xf6}),
            std::nullopt);
  // wrong version
  EXPECT_EQ(CID::fromBytes({0x02, 0x71, 0x00, 0x01, 0xf6}), std::nullopt);
  // missing version
  EXPECT_EQ(CID::fromBytes({0x71, 0x00, 0x01, 0xf6}), std::nullopt);

  // unsupported content type
  EXPECT_EQ(CID::fromBytes({0x01, 0x70, 0x00, 0x01, 0xf6}), std::nullopt);

  // unsupported hash type
  EXPECT_EQ(CID::fromBytes({0x01, 0x71, 0x12, 0x01, 0xf6}), std::nullopt);

  // extra byte in length VarInt
  EXPECT_EQ(CID::fromBytes({0x01, 0x71, 0x00, 0x81, 0x00, 0xf6}), std::nullopt);

  // extra byte
  EXPECT_EQ(CID::fromBytes({0x01, 0x71, 0x00, 0x01, 0xf6, 0x00}), std::nullopt);
  // missing byte
  EXPECT_EQ(CID::fromBytes({0x01, 0x71, 0x00, 0x01}), std::nullopt);

  // extra byte
  EXPECT_EQ(CID::fromBytes({0x01, 0x55, 0xa0, 0xe4, 0x02, 0x20, 0x01, 0xf4,
                            0xb7, 0x88, 0x59, 0x3d, 0x4f, 0x70, 0xde, 0x2a,
                            0x45, 0xc2, 0xe1, 0xe8, 0x70, 0x88, 0xbf, 0xbd,
                            0xfa, 0x29, 0x57, 0x7a, 0xe1, 0xb6, 0x2a, 0xba,
                            0x60, 0xe0, 0x95, 0xe3, 0xab, 0x53, 0x00}),
            std::nullopt);
  // missing byte
  EXPECT_EQ(CID::fromBytes({0x01, 0x55, 0xa0, 0xe4, 0x02, 0x20, 0x01, 0xf4,
                            0xb7, 0x88, 0x59, 0x3d, 0x4f, 0x70, 0xde, 0x2a,
                            0x45, 0xc2, 0xe1, 0xe8, 0x70, 0x88, 0xbf, 0xbd,
                            0xfa, 0x29, 0x57, 0x7a, 0xe1, 0xb6, 0x2a, 0xba,
                            0x60, 0xe0, 0x95, 0xe3, 0xab}),
            std::nullopt);
  // wrong length
  EXPECT_EQ(CID::fromBytes({0x01, 0x55, 0xa0, 0xe4, 0x02, 0x1f, 0x01, 0xf4,
                            0xb7, 0x88, 0x59, 0x3d, 0x4f, 0x70, 0xde, 0x2a,
                            0x45, 0xc2, 0xe1, 0xe8, 0x70, 0x88, 0xbf, 0xbd,
                            0xfa, 0x29, 0x57, 0x7a, 0xe1, 0xb6, 0x2a, 0xba,
                            0x60, 0xe0, 0x95, 0xe3, 0xab}),
            std::nullopt);
}

TEST(CIDTest, FromInvalidString) {
  EXPECT_EQ(CID::parse(""), std::nullopt);
  EXPECT_EQ(CID::parse("@"), std::nullopt);
}

TEST(CIDTest, from_base16) {
  EXPECT_EQ(CID::parse("f01550000"), identityCID({}));
  EXPECT_EQ(CID::parse("f0155000100"), identityCID({0x00}));
  EXPECT_EQ(CID::parse("f015500020000"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("f015500080123456789abcdef"),
            identityCID({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}));

  EXPECT_EQ(CID::parse("f"), std::nullopt);
  EXPECT_EQ(CID::parse("f0155000"), std::nullopt);
  EXPECT_EQ(CID::parse("f015500000"), std::nullopt);
  EXPECT_EQ(CID::parse("f015500010F"), std::nullopt);
  EXPECT_EQ(CID::parse("f015500010g"), std::nullopt);
}

TEST(CIDTest, to_base16) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base16);
  };
  EXPECT_EQ("f01550000", to({}));
  EXPECT_EQ("f0155000100", to({0x00}));
  EXPECT_EQ("f015500020000", to({0x00, 0x00}));
  EXPECT_EQ("f015500080123456789abcdef",
            to({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}));
}

TEST(CIDTest, from_base16upper) {
  EXPECT_EQ(CID::parse("F01550000"), identityCID({}));
  EXPECT_EQ(CID::parse("F0155000100"), identityCID({0x00}));
  EXPECT_EQ(CID::parse("F015500020000"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("F015500080123456789ABCDEF"),
            identityCID({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}));

  EXPECT_EQ(CID::parse("F"), std::nullopt);
  EXPECT_EQ(CID::parse("F0155000"), std::nullopt);
  EXPECT_EQ(CID::parse("F015500000"), std::nullopt);
  EXPECT_EQ(CID::parse("F015500010f"), std::nullopt);
  EXPECT_EQ(CID::parse("F015500010G"), std::nullopt);
}

TEST(CIDTest, to_base16upper) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base16upper);
  };
  EXPECT_EQ("F01550000", to({}));
  EXPECT_EQ("F0155000100", to({0x00}));
  EXPECT_EQ("F015500020000", to({0x00, 0x00}));
  EXPECT_EQ("F015500080123456789ABCDEF",
            to({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}));
}

TEST(CIDTest, from_base32) {
  EXPECT_EQ(CID::parse("bafkqaaa"), identityCID({}));
  EXPECT_EQ(CID::parse("bafkqaaia"), identityCID({0x00}));
  EXPECT_EQ(CID::parse("bafkqaaqaaa"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("bafkqaayaaaaa"), identityCID({0x00, 0x00, 0x00}));
  EXPECT_EQ(CID::parse("bafkqabaaaaaaa"),
            identityCID({0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(CID::parse("bafkqabiaaaaaaaa"),
            identityCID({0x00, 0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(CID::parse("bafkqafp7abcdefghijklmnopqrstuvwxyz234567"),
            identityCID({0xff, 0x00, 0x44, 0x32, 0x14, 0xc7, 0x42,
                         0x54, 0xb6, 0x35, 0xcf, 0x84, 0x65, 0x3a,
                         0x56, 0xd7, 0xc6, 0x75, 0xbe, 0x77, 0xdf}));

  EXPECT_EQ(CID::parse("b"), std::nullopt);
  EXPECT_EQ(CID::parse("bafkqaaa="), std::nullopt);
  EXPECT_EQ(CID::parse("bAfkqaaa"), std::nullopt);
  EXPECT_EQ(CID::parse("bafkqaaiaa"), std::nullopt);
}

TEST(CIDTest, to_base32) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base32);
  };
  EXPECT_EQ("bafkqaaa", to({}));
  EXPECT_EQ("bafkqaaia", to({0x00}));
  EXPECT_EQ("bafkqaaqaaa", to({0x00, 0x00}));
  EXPECT_EQ("bafkqaayaaaaa", to({0x00, 0x00, 0x00}));
  EXPECT_EQ("bafkqabaaaaaaa", to({0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ("bafkqabiaaaaaaaa", to({0x00, 0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(
      "bafkqafp7abcdefghijklmnopqrstuvwxyz234567",
      to({0xff, 0x00, 0x44, 0x32, 0x14, 0xc7, 0x42, 0x54, 0xb6, 0x35, 0xcf,
          0x84, 0x65, 0x3a, 0x56, 0xd7, 0xc6, 0x75, 0xbe, 0x77, 0xdf}));
}

TEST(CIDTest, from_base32upper) {
  EXPECT_EQ(CID::parse("BAFKQAAA"), identityCID({}));
  EXPECT_EQ(CID::parse("BAFKQAAIA"), identityCID({0x00}));
  EXPECT_EQ(CID::parse("BAFKQAAQAAA"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("BAFKQAAYAAAAA"), identityCID({0x00, 0x00, 0x00}));
  EXPECT_EQ(CID::parse("BAFKQABAAAAAAA"),
            identityCID({0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(CID::parse("BAFKQABIAAAAAAAA"),
            identityCID({0x00, 0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(CID::parse("BAFKQAFP7ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"),
            identityCID({0xff, 0x00, 0x44, 0x32, 0x14, 0xc7, 0x42,
                         0x54, 0xb6, 0x35, 0xcf, 0x84, 0x65, 0x3a,
                         0x56, 0xd7, 0xc6, 0x75, 0xbe, 0x77, 0xdf}));

  EXPECT_EQ(CID::parse("B"), std::nullopt);
  EXPECT_EQ(CID::parse("BAFKQAAA="), std::nullopt);
  EXPECT_EQ(CID::parse("BaFKQAAA"), std::nullopt);
  EXPECT_EQ(CID::parse("BAFKQAAIAA"), std::nullopt);
}

TEST(CIDTest, to_base32upper) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base32upper);
  };
  EXPECT_EQ("BAFKQAAA", to({}));
  EXPECT_EQ("BAFKQAAIA", to({0x00}));
  EXPECT_EQ("BAFKQAAQAAA", to({0x00, 0x00}));
  EXPECT_EQ("BAFKQAAYAAAAA", to({0x00, 0x00, 0x00}));
  EXPECT_EQ("BAFKQABAAAAAAA", to({0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ("BAFKQABIAAAAAAAA", to({0x00, 0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(
      "BAFKQAFP7ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
      to({0xff, 0x00, 0x44, 0x32, 0x14, 0xc7, 0x42, 0x54, 0xb6, 0x35, 0xcf,
          0x84, 0x65, 0x3a, 0x56, 0xd7, 0xc6, 0x75, 0xbe, 0x77, 0xdf}));
}

TEST(CIDTest, from_base64) {
  EXPECT_EQ(CID::parse("mAVUAAA"), identityCID({}));
  EXPECT_EQ(CID::parse("mAVUAAQA"), identityCID({0x00}));
  EXPECT_EQ(CID::parse("mAVUAAgAA"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("mAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrst"
                       "uvwxyz0123456789+/"),
            identityCID({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20,
                         0x92, 0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51,
                         0x55, 0x97, 0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82,
                         0x18, 0xa3, 0x92, 0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2,
                         0xdb, 0xaf, 0xc3, 0x1c, 0xb3, 0xd3, 0x5d, 0xb7, 0xe3,
                         0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));

  EXPECT_EQ(CID::parse("m"), std::nullopt);
  EXPECT_EQ(CID::parse("mAVUAAA=="), std::nullopt);
  EXPECT_EQ(CID::parse("mAVUAAgA_"), std::nullopt);
  EXPECT_EQ(CID::parse("mAVUAA==="), std::nullopt);
}

TEST(CIDTest, to_base64) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base64);
  };
  EXPECT_EQ("mAVUAAA", to({}));
  EXPECT_EQ("mAVUAAQA", to({0x00}));
  EXPECT_EQ("mAVUAAgAA", to({0x00, 0x00}));
  EXPECT_EQ("mAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234"
            "56789+/",
            to({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92,
                0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51, 0x55, 0x97,
                0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82, 0x18, 0xa3, 0x92,
                0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2, 0xdb, 0xaf, 0xc3, 0x1c,
                0xb3, 0xd3, 0x5d, 0xb7, 0xe3, 0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));
}

TEST(CIDTest, from_base64pad) {
  EXPECT_EQ(CID::parse("MAVUAAA=="), identityCID({}));
  EXPECT_EQ(CID::parse("MAVUAAQA="), identityCID({0x00}));
  EXPECT_EQ(CID::parse("MAVUAAgAA"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("MAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrst"
                       "uvwxyz0123456789+/"),
            identityCID({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20,
                         0x92, 0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51,
                         0x55, 0x97, 0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82,
                         0x18, 0xa3, 0x92, 0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2,
                         0xdb, 0xaf, 0xc3, 0x1c, 0xb3, 0xd3, 0x5d, 0xb7, 0xe3,
                         0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));

  EXPECT_EQ(CID::parse("M"), std::nullopt);
  EXPECT_EQ(CID::parse("MAVUAAQA"), std::nullopt);
  EXPECT_EQ(CID::parse("MAVUAAgAA===="), std::nullopt);
}

TEST(CIDTest, to_base64pad) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base64pad);
  };
  EXPECT_EQ("MAVUAAA==", to({}));
  EXPECT_EQ("MAVUAAQA=", to({0x00}));
  EXPECT_EQ("MAVUAAgAA", to({0x00, 0x00}));
  EXPECT_EQ("MAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234"
            "56789+/",
            to({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92,
                0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51, 0x55, 0x97,
                0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82, 0x18, 0xa3, 0x92,
                0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2, 0xdb, 0xaf, 0xc3, 0x1c,
                0xb3, 0xd3, 0x5d, 0xb7, 0xe3, 0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));
}

TEST(CIDTest, from_base64url) {
  EXPECT_EQ(CID::parse("uAVUAAA"), identityCID({}));
  EXPECT_EQ(CID::parse("uAVUAAQA"), identityCID({0x00}));
  EXPECT_EQ(CID::parse("uAVUAAgAA"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("uAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrst"
                       "uvwxyz0123456789-_"),
            identityCID({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20,
                         0x92, 0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51,
                         0x55, 0x97, 0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82,
                         0x18, 0xa3, 0x92, 0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2,
                         0xdb, 0xaf, 0xc3, 0x1c, 0xb3, 0xd3, 0x5d, 0xb7, 0xe3,
                         0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));

  EXPECT_EQ(CID::parse("u"), std::nullopt);
  EXPECT_EQ(CID::parse("uAVUAAA=="), std::nullopt);
  EXPECT_EQ(CID::parse("uAVUAAgA/"), std::nullopt);
  EXPECT_EQ(CID::parse("uAVUAA==="), std::nullopt);
}

TEST(CIDTest, to_base64url) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base64url);
  };
  EXPECT_EQ("uAVUAAA", to({}));
  EXPECT_EQ("uAVUAAQA", to({0x00}));
  EXPECT_EQ("uAVUAAgAA", to({0x00, 0x00}));
  EXPECT_EQ("uAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234"
            "56789-_",
            to({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92,
                0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51, 0x55, 0x97,
                0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82, 0x18, 0xa3, 0x92,
                0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2, 0xdb, 0xaf, 0xc3, 0x1c,
                0xb3, 0xd3, 0x5d, 0xb7, 0xe3, 0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));
}

TEST(CIDTest, from_base64urlpad) {
  EXPECT_EQ(CID::parse("UAVUAAA=="), identityCID({}));
  EXPECT_EQ(CID::parse("UAVUAAQA="), identityCID({0x00}));
  EXPECT_EQ(CID::parse("UAVUAAgAA"), identityCID({0x00, 0x00}));
  EXPECT_EQ(CID::parse("UAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrst"
                       "uvwxyz0123456789-_"),
            identityCID({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20,
                         0x92, 0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51,
                         0x55, 0x97, 0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82,
                         0x18, 0xa3, 0x92, 0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2,
                         0xdb, 0xaf, 0xc3, 0x1c, 0xb3, 0xd3, 0x5d, 0xb7, 0xe3,
                         0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));

  EXPECT_EQ(CID::parse("U"), std::nullopt);
  EXPECT_EQ(CID::parse("UAVUAAQA"), std::nullopt);
  EXPECT_EQ(CID::parse("UAVUAAgAA===="), std::nullopt);
}

TEST(CIDTest, to_base64urlpad) {
  auto to = [](llvm::ArrayRef<std::uint8_t> Bytes) {
    return identityCID(Bytes).asString(Multibase::base64urlpad);
  };
  EXPECT_EQ("UAVUAAA==", to({}));
  EXPECT_EQ("UAVUAAQA=", to({0x00}));
  EXPECT_EQ("UAVUAAgAA", to({0x00, 0x00}));
  EXPECT_EQ("UAVUAMlWqABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234"
            "56789-_",
            to({0x55, 0xaa, 0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92,
                0x8b, 0x30, 0xd3, 0x8f, 0x41, 0x14, 0x93, 0x51, 0x55, 0x97,
                0x61, 0x96, 0x9b, 0x71, 0xd7, 0x9f, 0x82, 0x18, 0xa3, 0x92,
                0x59, 0xa7, 0xa2, 0x9a, 0xab, 0xb2, 0xdb, 0xaf, 0xc3, 0x1c,
                0xb3, 0xd3, 0x5d, 0xb7, 0xe3, 0x9e, 0xbb, 0xf3, 0xdf, 0xbf}));
}

} // namespace
