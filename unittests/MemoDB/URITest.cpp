#include "memodb/Support.h"

#include <optional>

#include "gtest/gtest.h"

using namespace memodb;

namespace {

typedef std::vector<std::string> Segments;
typedef std::vector<std::string> Params;

TEST(URITest, ParseBasic) {
  auto uri = URI::parse("scheme://authority:0080/path?query#fragment");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "scheme");
  EXPECT_EQ(uri->host, "authority");
  EXPECT_EQ(uri->port, 80);
  EXPECT_EQ(uri->fragment, "fragment");
  EXPECT_EQ(uri->rootless, false);
  EXPECT_EQ(uri->path_segments, Segments({"path"}));
  EXPECT_EQ(uri->query_params, Params({"query"}));
}

TEST(URITest, ParseCase) {
  auto uri = URI::parse("SCHEME://AUTHORITY:0080/PATH?QUERY#FRAGMENT");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "scheme");
  EXPECT_EQ(uri->host, "authority");
  EXPECT_EQ(uri->port, 80);
  EXPECT_EQ(uri->fragment, "FRAGMENT");
  EXPECT_EQ(uri->rootless, false);
  EXPECT_EQ(uri->path_segments, Segments({"PATH"}));
  EXPECT_EQ(uri->query_params, Params({"QUERY"}));
}

TEST(URITest, ParsePercent) {
  auto uri =
      URI::parse("scheme://auth%6Frity/path%2fwith/slash?qu%65ry#fr%61gment");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "scheme");
  EXPECT_EQ(uri->host, "authority");
  EXPECT_EQ(uri->port, 0);
  EXPECT_EQ(uri->fragment, "fragment");
  EXPECT_EQ(uri->rootless, false);
  EXPECT_EQ(uri->path_segments, Segments({"path/with", "slash"}));
  EXPECT_EQ(uri->query_params, Params({"query"}));
}

TEST(URITest, ParseMinimal) {
  auto uri = URI::parse("x:");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "x");
  EXPECT_EQ(uri->host, "");
  EXPECT_EQ(uri->port, 0);
  EXPECT_EQ(uri->fragment, "");
  EXPECT_EQ(uri->rootless, true);
  EXPECT_EQ(uri->path_segments, Segments({}));
  EXPECT_EQ(uri->query_params, Params({}));
}

TEST(URITest, ParseEmptyPort) {
  auto uri = URI::parse("http://127.0.0.1:");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "http");
  EXPECT_EQ(uri->host, "127.0.0.1");
  EXPECT_EQ(uri->port, 0);
  EXPECT_EQ(uri->fragment, "");
  EXPECT_EQ(uri->rootless, true);
  EXPECT_EQ(uri->path_segments, Segments({}));
  EXPECT_EQ(uri->query_params, Params({}));
}

TEST(URITest, ParseEmptyFragment) {
  auto uri = URI::parse("/x#");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "");
  EXPECT_EQ(uri->host, "");
  EXPECT_EQ(uri->port, 0);
  EXPECT_EQ(uri->fragment, "");
  EXPECT_EQ(uri->rootless, false);
  EXPECT_EQ(uri->path_segments, Segments({"x"}));
  EXPECT_EQ(uri->query_params, Params({}));
}

TEST(URITest, ParseAbsolutePath) {
  auto uri = URI::parse("x:/y?a=b");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "x");
  EXPECT_EQ(uri->host, "");
  EXPECT_EQ(uri->port, 0);
  EXPECT_EQ(uri->fragment, "");
  EXPECT_EQ(uri->rootless, false);
  EXPECT_EQ(uri->path_segments, Segments({"y"}));
  EXPECT_EQ(uri->query_params, Params({"a=b"}));
}

TEST(URITest, ParseRootlessPath) {
  auto uri = URI::parse("x:y");
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->scheme, "x");
  EXPECT_EQ(uri->host, "");
  EXPECT_EQ(uri->port, 0);
  EXPECT_EQ(uri->fragment, "");
  EXPECT_EQ(uri->rootless, true);
  EXPECT_EQ(uri->path_segments, Segments({"y"}));
  EXPECT_EQ(uri->query_params, Params({}));
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
  auto uri = URI::parse("../../../../../../etc/passwd");
  EXPECT_EQ(uri, std::nullopt);

  uri = URI::parse("%2E%2E/xyz");
  EXPECT_EQ(uri, std::nullopt);

  uri = URI::parse("../../../../../../etc/passwd", /*allow_dot_segments*/ true);
  EXPECT_TRUE(uri != std::nullopt);
  EXPECT_EQ(uri->path_segments,
            Segments({"..", "..", "..", "..", "..", "..", "etc", "passwd"}));
}

TEST(URITest, EncodeBasic) {
  URI uri;
  uri.scheme = "scheme";
  uri.host = "authority";
  uri.port = 80;
  uri.path_segments.push_back("path");
  uri.query_params.push_back("query");
  uri.fragment = "fragment";
  EXPECT_EQ("scheme://authority:80/path?query#fragment", uri.encode());
}

TEST(URITest, EncodeEscaped) {
  URI uri;
  uri.path_segments = {std::string("\x00\x01\x02\x03\x04\x05\x06\x07", 8)};
  EXPECT_EQ("/%00%01%02%03%04%05%06%07", uri.encode());
  uri.path_segments = {std::string("\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f", 8)};
  EXPECT_EQ("/%08%09%0A%0B%0C%0D%0E%0F", uri.encode());
  uri.path_segments = {std::string("\x10\x11\x12\x13\x14\x15\x16\x17", 8)};
  EXPECT_EQ("/%10%11%12%13%14%15%16%17", uri.encode());
  uri.path_segments = {std::string("\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f", 8)};
  EXPECT_EQ("/%18%19%1A%1B%1C%1D%1E%1F", uri.encode());
  uri.path_segments = {" !\"#$%&'()*+,-./"};
  EXPECT_EQ("/%20!%22%23$%25&'()*+,-.%2F", uri.encode());
  uri.path_segments = {"0123456789:;<=>?"};
  EXPECT_EQ("/0123456789:;%3C=%3E%3F", uri.encode());
  uri.path_segments = {"@ABCDEFGHIJKLMNO"};
  EXPECT_EQ("/@ABCDEFGHIJKLMNO", uri.encode());
  uri.path_segments = {"PQRSTUVWXYZ[\\]^_"};
  EXPECT_EQ("/PQRSTUVWXYZ%5B%5C%5D%5E_", uri.encode());
  uri.path_segments = {"`abcdefghijklmno"};
  EXPECT_EQ("/%60abcdefghijklmno", uri.encode());
  uri.path_segments = {"pqrstuvwxyz{|}~\x7f"};
  EXPECT_EQ("/pqrstuvwxyz%7B%7C%7D~%7F", uri.encode());
  uri.path_segments = {"\x80\x81\x82\x83\x84\x85\x86\x87"};
  EXPECT_EQ("/%80%81%82%83%84%85%86%87", uri.encode());
}

} // end anonymous namespace
