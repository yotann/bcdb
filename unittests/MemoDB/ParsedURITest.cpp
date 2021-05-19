#include "memodb/memodb.h"

#include "gtest/gtest.h"

namespace {

typedef std::vector<std::string> Segments;

TEST(ParsedURITest, Basic) {
  ParsedURI ParsedURI("scheme://authority/path?query#fragment");
  EXPECT_EQ(ParsedURI.Scheme, "scheme");
  EXPECT_EQ(ParsedURI.Authority, "authority");
  EXPECT_EQ(ParsedURI.Path, "/path");
  EXPECT_EQ(ParsedURI.Query, "query");
  EXPECT_EQ(ParsedURI.Fragment, "fragment");
  EXPECT_EQ(ParsedURI.PathSegments, Segments({"", "path"}));
}

TEST(ParsedURITest, Percent) {
  ParsedURI ParsedURI(
      "scheme://auth%6Frity/path%2Fwith/slash?qu%65ry#fr%61gment");
  EXPECT_EQ(ParsedURI.Scheme, "scheme");
  EXPECT_EQ(ParsedURI.Authority, "authority");
  EXPECT_EQ(ParsedURI.Path, "/path/with/slash");
  EXPECT_EQ(ParsedURI.Query, "query");
  EXPECT_EQ(ParsedURI.Fragment, "fragment");
  EXPECT_EQ(ParsedURI.PathSegments, Segments({"", "path/with", "slash"}));
}

TEST(ParsedURITest, Minimal) {
  ParsedURI ParsedURI("x:");
  EXPECT_EQ(ParsedURI.Scheme, "x");
  EXPECT_EQ(ParsedURI.Authority, "");
  EXPECT_EQ(ParsedURI.Path, "");
  EXPECT_EQ(ParsedURI.Query, "");
  EXPECT_EQ(ParsedURI.Fragment, "");
  EXPECT_EQ(ParsedURI.PathSegments, Segments({""}));
}

TEST(ParsedURITest, AbsolutePath) {
  ParsedURI ParsedURI("x:/y?a=b");
  EXPECT_EQ(ParsedURI.Scheme, "x");
  EXPECT_EQ(ParsedURI.Authority, "");
  EXPECT_EQ(ParsedURI.Path, "/y");
  EXPECT_EQ(ParsedURI.Query, "a=b");
  EXPECT_EQ(ParsedURI.Fragment, "");
  EXPECT_EQ(ParsedURI.PathSegments, Segments({"", "y"}));
}

TEST(ParsedURITest, RelativePath) {
  ParsedURI ParsedURI("x:y?a=b");
  EXPECT_EQ(ParsedURI.Scheme, "x");
  EXPECT_EQ(ParsedURI.Authority, "");
  EXPECT_EQ(ParsedURI.Path, "y");
  EXPECT_EQ(ParsedURI.Query, "a=b");
  EXPECT_EQ(ParsedURI.Fragment, "");
  EXPECT_EQ(ParsedURI.PathSegments, Segments({"y"}));
}

} // end anonymous namespace
