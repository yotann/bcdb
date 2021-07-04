#include "memodb/Support.h"

#include <optional>

#include "gtest/gtest.h"

using namespace memodb;

namespace {

typedef std::vector<std::string> Segments;
typedef std::vector<std::string> Params;

TEST(URITest, ParseBasic) {
  auto uri = URI::parse("scheme://authority/path?query#fragment");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "scheme");
  EXPECT_EQ(uri->authority, "authority");
  EXPECT_EQ(uri->fragment, "fragment");
  EXPECT_EQ(uri->path_segments, Segments({"path"}));
  EXPECT_EQ(uri->query_params, Params({"query"}));
}

TEST(URITest, ParsePercent) {
  auto uri =
      URI::parse("scheme://auth%6Frity/path%2fwith/slash?qu%65ry#fr%61gment");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "scheme");
  EXPECT_EQ(uri->authority, "authority");
  EXPECT_EQ(uri->fragment, "fragment");
  EXPECT_EQ(uri->path_segments, Segments({"path/with", "slash"}));
  EXPECT_EQ(uri->query_params, Params({"query"}));
}

TEST(URITest, ParseMinimal) {
  auto uri = URI::parse("x:");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "x");
  EXPECT_EQ(uri->authority, "");
  EXPECT_EQ(uri->fragment, "");
  EXPECT_EQ(uri->path_segments, Segments({}));
  EXPECT_EQ(uri->query_params, Params({}));
}

TEST(URITest, ParseAbsolutePath) {
  auto uri = URI::parse("x:/y?a=b");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "x");
  EXPECT_EQ(uri->authority, "");
  EXPECT_EQ(uri->fragment, "");
  EXPECT_EQ(uri->path_segments, Segments({"y"}));
  EXPECT_EQ(uri->query_params, Params({"a=b"}));
}

TEST(URITest, ParseRelativePath) {
  auto uri = URI::parse("x:y");
  EXPECT_EQ(uri, std::nullopt);
}

TEST(URITest, ParsePercentNonHex) {
  auto uri = URI::parse("scheme://authority/%0gpath");
  EXPECT_EQ(uri, std::nullopt);
}

TEST(URITest, ParsePercentNotEnoughChars) {
  auto uri = URI::parse("scheme://authority/foo%0");
  EXPECT_EQ(uri, std::nullopt);
}

TEST(URITest, ParseParentDirectory) {
  auto uri = URI::parse("/../../../../../../etc/passwd");
  EXPECT_EQ(uri, std::nullopt);
}

} // end anonymous namespace
