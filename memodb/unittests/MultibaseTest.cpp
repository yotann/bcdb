#include "memodb/Multibase.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <llvm/ADT/StringRef.h>

#include "gtest/gtest.h"

using namespace memodb;
using llvm::StringRef;
using Bytes = std::vector<std::uint8_t>;

namespace {

TEST(Multibase, DecodeEmpty) {
  EXPECT_EQ(Multibase::base64.decodeWithoutPrefix(""), Bytes());
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix(""), Bytes());
}

TEST(Multibase, DecodePadAmounts) {
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("aaaaaaaa"),
            Bytes({0, 0, 0, 0, 0}));
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("aaaaaaa"),
            Bytes({0, 0, 0, 0}));
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("aaaaaa"), std::nullopt);
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("aaaaa"), Bytes({0, 0, 0}));
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("aaaa"), Bytes({0, 0}));
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("aaa"), std::nullopt);
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("aa"), Bytes({0}));
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("a"), std::nullopt);

  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("AAAA"), Bytes({0, 0, 0}));
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("AAA="), Bytes({0, 0}));
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("AA=="), Bytes({0}));
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("A==="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("===="), std::nullopt);
}

TEST(Multibase, DecodeNonzeroPadding) {
  // These values are technically invalid, but other decoders generally accept
  // them.
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("ab"), Bytes({0}));
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("AB=="), Bytes({0}));
}

TEST(Multibase, DecodeInvalidChar) {
  EXPECT_EQ(Multibase::base32.decodeWithoutPrefix("AA"), std::nullopt);
  EXPECT_EQ(Multibase::base64.decodeWithoutPrefix(StringRef("A\x00", 2)),
            std::nullopt);
  EXPECT_EQ(Multibase::base64.decodeWithoutPrefix(StringRef("A\x80", 2)),
            std::nullopt);
  EXPECT_EQ(Multibase::base64.decodeWithoutPrefix(StringRef("A\xff", 2)),
            std::nullopt);
  EXPECT_EQ(Multibase::base64.decodeWithoutPrefix(StringRef("A_", 2)),
            std::nullopt);
}

TEST(Multibase, DecodePaddingInMiddle) {
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("ab=c"), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("a=bc"), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("=abc"), std::nullopt);
}

TEST(Multibase, DecodeExtraPad) {
  // A bug in .NET:
  // https://detunized.net/posts/2019-03-06-base64-decoding-bug-that-is-present-in-all-version-of-.net/
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abc=="), std::nullopt);

  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abcd="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("ab==="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("a===="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("====="), std::nullopt);

  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abcd=="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abc==="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("ab===="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("a====="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("======"), std::nullopt);

  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abcd==="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abc===="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("ab====="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("a======"), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("======="), std::nullopt);

  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abcd===="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("abc====="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("ab======"), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("a======="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("========"), std::nullopt);

  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("===="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("==="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("=="), std::nullopt);
  EXPECT_EQ(Multibase::base64pad.decodeWithoutPrefix("="), std::nullopt);
}

} // namespace
