#include "memodb/HTTP.h"

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <optional>
#include <string>

#include "MockStore.h"
#include "memodb/CID.h"
#include "memodb/Node.h"
#include "memodb/URI.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace memodb;
using llvm::StringRef;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Expectation;
using ::testing::ExpectationSet;
using ::testing::Return;
using ::testing::StrCaseEq;

namespace {

MATCHER_P(TwineEq, string, "") { return StringRef(string).equals(arg.str()); }

MATCHER_P(TwineCaseEq, string, "") {
  return StringRef(string).equals_lower(arg.str());
}

class MockHTTPRequest : public HTTPRequest {
public:
  MOCK_METHOD(StringRef, getMethodString, (), (const, override));
  MOCK_METHOD(std::optional<URI>, getURI, (), (const, override));
  MOCK_METHOD(std::optional<llvm::StringRef>, getHeader,
              (const llvm::Twine &key), (const, override));
  MOCK_METHOD(StringRef, getBody, (), (const, override));
  MOCK_METHOD(void, sendStatus, (std::uint16_t status), (override));
  MOCK_METHOD(void, sendHeader, (StringRef key, const llvm::Twine &value),
              (override));
  MOCK_METHOD(void, sendBody, (const llvm::Twine &body), (override));
  MOCK_METHOD(void, sendEmptyBody, (), (override));
};

TEST(HTTPTest, GetMethodLowercase) {
  MockHTTPRequest request;
  EXPECT_CALL(request, getMethodString).WillOnce(Return("get"));
  EXPECT_EQ(request.getMethod(), Request::Method::GET);
}

TEST(HTTPTest, GetMethodUppercase) {
  MockHTTPRequest request;
  EXPECT_CALL(request, getMethodString).WillOnce(Return("POST"));
  EXPECT_EQ(request.getMethod(), Request::Method::POST);
}

TEST(HTTPTest, GetMethodUnknown) {
  MockHTTPRequest request;
  EXPECT_CALL(request, getMethodString).WillOnce(Return("DANCE"));
  EXPECT_EQ(request.getMethod(), std::nullopt);
}

TEST(HTTPTest, GetContentNodeCBOR) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getBody)
      .WillOnce(Return(StringRef("\x82\x01\x61\x32", 4)));
  EXPECT_CALL(request, getHeader(TwineCaseEq("content-type")))
      .WillOnce(Return("application/cbor"));
  EXPECT_CALL(request, sendStatus).Times(0);
  EXPECT_EQ(request.getContentNode(store), Node(node_list_arg, {1, "2"}));
}

TEST(HTTPTest, GetContentNodeJSON) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getBody).WillOnce(Return("[1,\"2\"]"));
  EXPECT_CALL(request, getHeader(TwineCaseEq("content-type")))
      .WillOnce(Return("application/json"));
  EXPECT_CALL(request, sendStatus).Times(0);
  EXPECT_EQ(request.getContentNode(store), Node(node_list_arg, {1, "2"}));
}

TEST(HTTPTest, GetContentNodeOctetStream) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getBody).WillOnce(Return("test"));
  EXPECT_CALL(request, getHeader(TwineCaseEq("content-type")))
      .WillOnce(Return("application/octet-stream"));
  EXPECT_CALL(request, sendStatus).Times(0);
  EXPECT_EQ(request.getContentNode(store),
            Node(byte_string_arg, StringRef("test")));
}

TEST(HTTPTest, GetContentNodeUnsupported) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getBody).WillOnce(Return("test"));
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("content-type")))
      .WillOnce(Return("text/plain"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(415)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                      TwineEq("application/problem+json")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request,
              sendBody(TwineEq(
                  "{\"title\":\"Unsupported Media Type\",\"status\":415}")))
      .Times(1)
      .After(headers);
  EXPECT_EQ(request.getContentNode(store), std::nullopt);
}

TEST(HTTPTest, GetContentNodeCBORInvalid) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getBody).WillOnce(Return(StringRef("\x82\x01\x61", 3)));
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("content-type")))
      .WillOnce(Return("application/cbor"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(400)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                      TwineEq("application/problem+json")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request,
              sendBody(TwineEq(
                  "{\"type\":\"/problems/"
                  "invalid-or-unsupported-cbor\",\"title\":\"Invalid "
                  "or unsupported CBOR\",\"status\":400,\"detail\":\"Invalid "
                  "CBOR: missing "
                  "data from string\"}")))
      .Times(1)
      .After(headers);
  EXPECT_EQ(request.getContentNode(store), std::nullopt);
}

TEST(HTTPTest, GetContentNodeJSONInvalidSyntax) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getBody).WillOnce(Return("{"));
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("content-type")))
      .WillOnce(Return("application/json"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(400)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                      TwineEq("application/problem+json")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request,
              sendBody(TwineEq(
                  "{\"type\":\"/problems/"
                  "invalid-or-unsupported-json\",\"title\":\"Invalid "
                  "or unsupported JSON\",\"status\":400,\"detail\":\"Invalid "
                  "MemoDB JSON: "
                  "Expected '\\\"'\"}")))
      .Times(1)
      .After(headers);
  EXPECT_EQ(request.getContentNode(store), std::nullopt);
}

TEST(HTTPTest, GetContentNodeJSONInvalidNode) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getBody).WillOnce(Return("{\"one\":1}"));
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("content-type")))
      .WillOnce(Return("application/json"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(400)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                      TwineEq("application/problem+json")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request,
              sendBody(TwineEq(
                  "{\"type\":\"/problems/"
                  "invalid-or-unsupported-json\",\"title\":\"Invalid "
                  "or unsupported JSON\",\"status\":400,\"detail\":\"Invalid "
                  "MemoDB JSON: "
                  "Invalid special JSON object\"}")))
      .Times(1)
      .After(headers);
  EXPECT_EQ(request.getContentNode(store), std::nullopt);
}

TEST(HTTPTest, SendContentNodeCBOR) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("accept")))
      .WillRepeatedly(Return("application/cbor"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(200)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("cache-control"),
                                      TwineEq("max-age=0, must-revalidate")))
          .Times(1)
          .After(status);
  headers += EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                             TwineEq("application/cbor")))
                 .Times(1)
                 .After(status);
  headers += EXPECT_CALL(request, sendHeader(TwineCaseEq("etag"),
                                             TwineEq("\"cbor+uAXEAAQw\"")))
                 .Times(1)
                 .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("server"), TwineEq("MemoDB")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("vary"),
                                      TwineEq("Accept, Accept-Encoding")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request, sendBody(TwineEq(StringRef("\x0c", 1))))
      .Times(1)
      .After(headers);
  request.sendContentNode(Node(12), *CID::parse("uAXEAAQw"),
                          Request::CacheControl::Mutable);
}

TEST(HTTPTest, SendContentNodeJSON) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("accept")))
      .WillRepeatedly(Return("application/json"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(200)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("cache-control"),
                                      TwineEq("max-age=0, must-revalidate")))
          .Times(1)
          .After(status);
  headers += EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                             TwineEq("application/json")))
                 .Times(1)
                 .After(status);
  headers += EXPECT_CALL(request, sendHeader(TwineCaseEq("etag"),
                                             TwineEq("\"json+uAXEAAQw\"")))
                 .Times(1)
                 .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("server"), TwineEq("MemoDB")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("vary"),
                                      TwineEq("Accept, Accept-Encoding")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request, sendBody(TwineEq("12"))).Times(1).After(headers);
  request.sendContentNode(Node(12), std::nullopt,
                          Request::CacheControl::Ephemeral);
}

TEST(HTTPTest, SendContentNodeAcceptAll) {
  // Curl, and Python's requests module, send "Accept: */*" by default. We want
  // to respond with JSON in these cases.
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("accept")))
      .WillRepeatedly(Return("*/*"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(200)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("cache-control"),
                                      TwineEq("max-age=0, must-revalidate")))
          .Times(1)
          .After(status);
  headers += EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                             TwineEq("application/json")))
                 .Times(1)
                 .After(status);
  headers += EXPECT_CALL(request, sendHeader(TwineCaseEq("etag"),
                                             TwineEq("\"json+uAXEAAQw\"")))
                 .Times(1)
                 .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("server"), TwineEq("MemoDB")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("vary"),
                                      TwineEq("Accept, Accept-Encoding")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request, sendBody(TwineEq("12"))).Times(1).After(headers);
  request.sendContentNode(Node(12), std::nullopt,
                          Request::CacheControl::Ephemeral);
}

TEST(HTTPTest, SendContentNodeOctetStream) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, getHeader(TwineCaseEq("accept")))
      .WillRepeatedly(
          Return("application/octet-stream;q=0.1,application/json;q=0.01"));
  EXPECT_CALL(request, sendEmptyBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(200)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("cache-control"),
                                      TwineEq("max-age=604800, immutable")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"),
                                      TwineEq("application/octet-stream")))
          .Times(1)
          .After(status);
  headers += EXPECT_CALL(request, sendHeader(TwineCaseEq("etag"),
                                             TwineEq("\"raw+uAVUAAjEy\"")))
                 .Times(1)
                 .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("server"), TwineEq("MemoDB")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("vary"),
                                      TwineEq("Accept, Accept-Encoding")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request, sendBody(TwineEq("12"))).Times(1).After(headers);
  request.sendContentNode(Node(byte_string_arg, StringRef("12")), std::nullopt,
                          Request::CacheControl::Immutable);
}

TEST(HTTPTest, SendCreated) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, sendBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(201)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("cache-control"),
                                      TwineEq("max-age=0, must-revalidate")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("server"), TwineEq("MemoDB")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("vary"),
                                      TwineEq("Accept, Accept-Encoding")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"), _)).Times(0);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("etag"), _)).Times(0);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("location"), _)).Times(0);
  EXPECT_CALL(request, sendEmptyBody).Times(1).After(headers);
  request.sendCreated(std::nullopt);
}

TEST(HTTPTest, SendCreatedPath) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, sendBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(201)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("cache-control"),
                                      TwineEq("max-age=0, must-revalidate")))
          .Times(1)
          .After(status);
  headers += EXPECT_CALL(request,
                         sendHeader(TwineCaseEq("location"), TwineEq("/cid/2")))
                 .Times(1)
                 .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("server"), TwineEq("MemoDB")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("vary"),
                                      TwineEq("Accept, Accept-Encoding")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"), _)).Times(0);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("etag"), _)).Times(0);
  EXPECT_CALL(request, sendEmptyBody).Times(1).After(headers);
  URI path;
  path.path_segments = {"cid", "2"};
  request.sendCreated(path);
}

TEST(HTTPTest, SendDeleted) {
  MockStore store;
  MockHTTPRequest request;
  EXPECT_CALL(request, getHeader).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(request, sendBody).Times(0);
  Expectation status = EXPECT_CALL(request, sendStatus(204)).Times(1);
  ExpectationSet headers;
  headers += EXPECT_CALL(request, sendHeader).Times(AnyNumber()).After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("cache-control"),
                                      TwineEq("max-age=0, must-revalidate")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("server"), TwineEq("MemoDB")))
          .Times(1)
          .After(status);
  headers +=
      EXPECT_CALL(request, sendHeader(TwineCaseEq("vary"),
                                      TwineEq("Accept, Accept-Encoding")))
          .Times(1)
          .After(status);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("content-type"), _)).Times(0);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("etag"), _)).Times(0);
  EXPECT_CALL(request, sendHeader(TwineCaseEq("location"), _)).Times(0);
  EXPECT_CALL(request, sendEmptyBody).Times(1).After(headers);
  request.sendDeleted();
}

} // end anonymous namespace
