#include "memodb/HTTP.h"

#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Node.h"
#include "memodb/URI.h"
#include "gtest/gtest.h"

using namespace memodb;
using llvm::StringRef;

namespace {

// TODO: use a mock class.
class TestHTTPRequest : public HTTPRequest {
public:
  TestHTTPRequest(StringRef method_str, std::optional<StringRef> uri_str,
                  StringRef body = "")
      : request_method_str(method_str),
        request_uri(uri_str ? URI::parse(*uri_str) : std::nullopt),
        request_body(body) {}

  StringRef getMethodString() const override { return request_method_str; }

  std::optional<URI> getURI() const override { return request_uri; }

  std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const override {
    auto key_lower = StringRef(key.str()).lower();
    auto iter = request_headers.find(key_lower);
    if (iter == request_headers.end())
      return std::nullopt;
    return iter->getValue();
  }

  StringRef getBody() const override { return request_body; }

  void sendStatus(std::uint16_t status) override {
    EXPECT_EQ(response_status, std::nullopt);
    response_status = status;
  }

  void sendHeader(StringRef key, const llvm::Twine &value) override {
    EXPECT_NE(response_status, std::nullopt);
    EXPECT_EQ(response_body, std::nullopt);
    auto key_lower = key.lower();
    EXPECT_EQ(response_headers.count(key_lower), 0);
    response_headers[key_lower] = value.str();
  }

  void sendBody(const llvm::Twine &body) override {
    EXPECT_NE(response_status, std::nullopt);
    EXPECT_EQ(response_body, std::nullopt);
    response_body = body.str();
  }

  void sendEmptyBody() override {
    EXPECT_NE(response_status, std::nullopt);
    EXPECT_EQ(response_body, std::nullopt);
    response_body = "";
  }

  std::string request_method_str;
  std::optional<URI> request_uri;
  llvm::StringMap<std::string> request_headers;
  std::string request_body;

  std::optional<std::uint16_t> response_status;
  llvm::StringMap<std::optional<std::string>> response_headers;
  std::optional<std::string> response_body;
};

TEST(HTTPTest, GetMethod) {
  EXPECT_EQ(TestHTTPRequest("get", "/cid").getMethod(), Request::Method::GET);
  EXPECT_EQ(TestHTTPRequest("POST", "/cid").getMethod(), Request::Method::POST);
  EXPECT_EQ(TestHTTPRequest("DANCE", "/cid").getMethod(), std::nullopt);
}

TEST(HTTPTest, GetContentNodeCBOR) {
  TestHTTPRequest request("POST", "/cid", StringRef("\x82\x01\x61\x32", 4));
  request.request_headers["content-type"] = "application/cbor";
  EXPECT_EQ(request.getContentNode(), Node(node_list_arg, {1, "2"}));
  EXPECT_EQ(request.response_status, std::nullopt);
}

TEST(HTTPTest, GetContentNodeJSON) {
  TestHTTPRequest request("POST", "/cid", "[1,\"2\"]");
  request.request_headers["content-type"] = "application/json";
  EXPECT_EQ(request.getContentNode(), Node(node_list_arg, {1, "2"}));
  EXPECT_EQ(request.response_status, std::nullopt);
}

TEST(HTTPTest, GetContentNodeOctetStream) {
  TestHTTPRequest request("POST", "/cid", "test");
  request.request_headers["content-type"] = "application/octet-stream";
  EXPECT_EQ(request.getContentNode(),
            Node(byte_string_arg, StringRef("test", 4)));
  EXPECT_EQ(request.response_status, std::nullopt);
}

TEST(HTTPTest, GetContentNodeUnsupported) {
  TestHTTPRequest request("POST", "/cid", "test");
  request.request_headers["content-type"] = "text/plain";
  EXPECT_EQ(request.getContentNode(), std::nullopt);
  EXPECT_EQ(request.response_status, 415);
  EXPECT_EQ(request.response_headers["content-type"],
            "application/problem+json");
  EXPECT_EQ(request.response_body,
            "{\"title\":\"Unsupported Media Type\",\"status\":415}");
}

TEST(HTTPTest, GetContentNodeCBORInvalid) {
  TestHTTPRequest request("POST", "/cid", StringRef("\x82\x01\x61", 3));
  request.request_headers["content-type"] = "application/cbor";
  EXPECT_EQ(request.getContentNode(), std::nullopt);
  EXPECT_EQ(request.response_status, 400);
  EXPECT_EQ(request.response_headers["content-type"],
            "application/problem+json");
  EXPECT_EQ(
      request.response_body,
      "{\"type\":\"/problems/invalid-or-unsupported-cbor\",\"title\":\"Invalid "
      "or unsupported CBOR\",\"status\":400,\"detail\":\"Invalid CBOR: missing "
      "data from string\"}");
}

TEST(HTTPTest, GetContentNodeJSONInvalidSyntax) {
  TestHTTPRequest request("POST", "/cid", "{");
  request.request_headers["content-type"] = "application/json";
  EXPECT_EQ(request.getContentNode(), std::nullopt);
  EXPECT_EQ(request.response_status, 400);
  EXPECT_EQ(request.response_headers["content-type"],
            "application/problem+json");
  EXPECT_EQ(
      request.response_body,
      "{\"type\":\"/problems/invalid-or-unsupported-json\",\"title\":\"Invalid "
      "or unsupported JSON\",\"status\":400,\"detail\":\"Invalid MemoDB JSON: "
      "Expected '\\\"'\"}");
}

TEST(HTTPTest, GetContentNodeJSONInvalidNode) {
  TestHTTPRequest request("POST", "/cid", "{\"one\":1}");
  request.request_headers["content-type"] = "application/json";
  EXPECT_EQ(request.getContentNode(), std::nullopt);
  EXPECT_EQ(request.response_status, 400);
  EXPECT_EQ(request.response_headers["content-type"],
            "application/problem+json");
  EXPECT_EQ(
      request.response_body,
      "{\"type\":\"/problems/invalid-or-unsupported-json\",\"title\":\"Invalid "
      "or unsupported JSON\",\"status\":400,\"detail\":\"Invalid MemoDB JSON: "
      "Invalid special JSON object\"}");
}

TEST(HTTPTest, SendContentNodeCBOR) {
  TestHTTPRequest request("GET", "/cid/foo");
  request.request_headers["accept"] = "application/cbor";
  request.sendContentNode(Node(12), *CID::parse("uAXEAAQw"),
                          Request::CacheControl::Mutable);
  EXPECT_EQ(request.response_status, 200);
  EXPECT_EQ(request.response_headers["cache-control"],
            "max-age=0, must-revalidate");
  EXPECT_EQ(request.response_headers["content-type"], "application/cbor");
  EXPECT_EQ(request.response_headers["etag"], "\"cbor+uAXEAAQw\"");
  EXPECT_EQ(request.response_headers["server"], "MemoDB");
  EXPECT_EQ(request.response_headers["vary"], "Accept, Accept-Encoding");
  EXPECT_EQ(request.response_body, StringRef("\x0c", 1));
}

TEST(HTTPTest, SendContentNodeJSON) {
  TestHTTPRequest request("GET", "/cid/foo");
  request.sendContentNode(Node(12), std::nullopt,
                          Request::CacheControl::Ephemeral);
  EXPECT_EQ(request.response_status, 200);
  EXPECT_EQ(request.response_headers["cache-control"],
            "max-age=0, must-revalidate");
  EXPECT_EQ(request.response_headers["content-type"], "application/json");
  EXPECT_EQ(request.response_headers["etag"], "\"json+uAXEAAQw\"");
  EXPECT_EQ(request.response_headers["server"], "MemoDB");
  EXPECT_EQ(request.response_headers["vary"], "Accept, Accept-Encoding");
  EXPECT_EQ(request.response_body, "12");
}

TEST(HTTPTest, SendContentNodeAcceptAll) {
  // Curl, and Python's requests module, send "Accept: */*" by default. We want
  // to respond with JSON in these cases.
  TestHTTPRequest request("GET", "/cid/foo");
  request.request_headers["accept"] = "*/*";
  request.sendContentNode(Node(12), std::nullopt,
                          Request::CacheControl::Ephemeral);
  EXPECT_EQ(request.response_status, 200);
  EXPECT_EQ(request.response_headers["cache-control"],
            "max-age=0, must-revalidate");
  EXPECT_EQ(request.response_headers["content-type"], "application/json");
  EXPECT_EQ(request.response_headers["etag"], "\"json+uAXEAAQw\"");
  EXPECT_EQ(request.response_headers["server"], "MemoDB");
  EXPECT_EQ(request.response_headers["vary"], "Accept, Accept-Encoding");
  EXPECT_EQ(request.response_body, "12");
}

TEST(HTTPTest, SendContentNodeOctetStream) {
  TestHTTPRequest request("GET", "/cid/foo");
  request.request_headers["accept"] =
      "application/octet-stream;q=0.1,application/json;q=0.01";
  request.sendContentNode(Node(byte_string_arg, StringRef("12", 2)),
                          std::nullopt, Request::CacheControl::Immutable);
  EXPECT_EQ(request.response_status, 200);
  EXPECT_EQ(request.response_headers["cache-control"],
            "max-age=604800, immutable");
  EXPECT_EQ(request.response_headers["content-type"],
            "application/octet-stream");
  EXPECT_EQ(request.response_headers["etag"], "\"raw+uAVUAAjEy\"");
  EXPECT_EQ(request.response_headers["server"], "MemoDB");
  EXPECT_EQ(request.response_headers["vary"], "Accept, Accept-Encoding");
  EXPECT_EQ(request.response_body, StringRef("12", 2));
}

TEST(HTTPTest, SendCreated) {
  TestHTTPRequest request("POST", "/cid");
  request.sendCreated(std::nullopt);
  EXPECT_EQ(request.response_status, 201);
  EXPECT_EQ(request.response_headers["cache-control"],
            "max-age=0, must-revalidate");
  EXPECT_EQ(request.response_headers["content-type"], std::nullopt);
  EXPECT_EQ(request.response_headers["etag"], std::nullopt);
  EXPECT_EQ(request.response_headers["location"], std::nullopt);
  EXPECT_EQ(request.response_headers["server"], "MemoDB");
  EXPECT_EQ(request.response_headers["vary"], "Accept, Accept-Encoding");
  EXPECT_EQ(request.response_body, "");
}

TEST(HTTPTest, SendCreatedPath) {
  TestHTTPRequest request("POST", "/cid");
  URI path;
  path.path_segments = {"cid", "2"};
  request.sendCreated(path);
  EXPECT_EQ(request.response_status, 201);
  EXPECT_EQ(request.response_headers["cache-control"],
            "max-age=0, must-revalidate");
  EXPECT_EQ(request.response_headers["content-type"], std::nullopt);
  EXPECT_EQ(request.response_headers["etag"], std::nullopt);
  EXPECT_EQ(request.response_headers["location"], "/cid/2");
  EXPECT_EQ(request.response_headers["server"], "MemoDB");
  EXPECT_EQ(request.response_headers["vary"], "Accept, Accept-Encoding");
  EXPECT_EQ(request.response_body, "");
}

TEST(HTTPTest, SendDeleted) {
  TestHTTPRequest request("POST", "/cid");
  request.sendDeleted();
  EXPECT_EQ(request.response_status, 204);
  EXPECT_EQ(request.response_headers["cache-control"],
            "max-age=0, must-revalidate");
  EXPECT_EQ(request.response_headers["content-type"], std::nullopt);
  EXPECT_EQ(request.response_headers["etag"], std::nullopt);
  EXPECT_EQ(request.response_headers["location"], std::nullopt);
  EXPECT_EQ(request.response_headers["server"], "MemoDB");
  EXPECT_EQ(request.response_headers["vary"], "Accept, Accept-Encoding");
  EXPECT_EQ(request.response_body, "");
}

} // end anonymous namespace
