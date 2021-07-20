#include "memodb/Server.h"

#include <llvm/ADT/StringRef.h>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Node.h"
#include "memodb/Store.h"
#include "memodb/URI.h"
#include "gtest/gtest.h"

using namespace memodb;
using llvm::StringRef;

namespace {

enum class ResponseType {
  Content,
  Created,
  Error,
  MethodNotAllowed,
};

class TestRequest : public Request {
public:
  TestRequest(std::optional<Method> method, std::optional<StringRef> uri_str,
              std::optional<Node> content_node = std::nullopt)
      : request_method(method),
        request_uri(uri_str ? URI::parse(*uri_str) : std::nullopt),
        request_content_node(content_node) {}

  std::optional<Method> getMethod() const override { return request_method; }

  std::optional<URI> getURI() const override { return request_uri; }

  std::optional<Node> getContentNode() override { return request_content_node; }

  void sendContentNode(const Node &node, const std::optional<CID> &cid_if_known,
                       CacheControl cache_control) override {
    EXPECT_EQ(response_type, std::nullopt);
    response_type = ResponseType::Content;
    response_content_node = node;
    response_content_cid = cid_if_known;
    response_cache_control = cache_control;
  }

  void sendCreated(const std::optional<URI> &path) override {
    EXPECT_EQ(response_type, std::nullopt);
    response_type = ResponseType::Created;
    response_location = path;
  }

  void sendError(Status status, std::optional<StringRef> type, StringRef title,
                 const std::optional<llvm::Twine> &detail) override {
    EXPECT_EQ(response_type, std::nullopt);
    response_type = ResponseType::Error;
    error_status = status;
    error_type = type;
    error_title = title;
    if (detail)
      error_detail = detail->str();
    else
      error_detail = std::nullopt;
  }

  void sendMethodNotAllowed(StringRef allow) override {
    EXPECT_EQ(response_type, std::nullopt);
    response_type = ResponseType::MethodNotAllowed;
    error_allowed_methods = allow;
  }

  std::optional<Method> request_method;
  std::optional<URI> request_uri;
  std::optional<Node> request_content_node;

  std::optional<ResponseType> response_type;

  std::optional<Node> response_content_node;
  std::optional<CID> response_content_cid;
  std::optional<CacheControl> response_cache_control;
  std::optional<URI> response_location;

  std::optional<Status> error_status;
  std::optional<std::string> error_type;
  std::optional<std::string> error_title;
  std::optional<std::string> error_detail;
  std::optional<std::string> error_allowed_methods;
};

TEST(ServerTest, UnknownMethod) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(std::nullopt, "/cid/uAXEAB2Zjb29raWU");
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Error);
  EXPECT_EQ(request.error_status, Request::Status::NotImplemented);
}

TEST(ServerTest, MethodNotAllowed) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(Request::Method::DELETE, "/cid");
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::MethodNotAllowed);
  EXPECT_EQ(request.error_allowed_methods, "POST");
}

TEST(ServerTest, DotSegmentsInURI) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(Request::Method::GET, "/cid/./uAXEAB2Zjb29raWU");
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Error);
  EXPECT_EQ(request.error_status, Request::Status::BadRequest);
}

TEST(ServerTest, GetCID) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(Request::Method::GET, "/cid/uAXEAB2Zjb29raWU");
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Content);
  EXPECT_EQ(request.response_content_node, Node("cookie"));
  EXPECT_EQ(request.response_content_cid, *CID::parse("uAXEAB2Zjb29raWU"));
  EXPECT_EQ(request.response_cache_control, Request::CacheControl::Immutable);
}

TEST(ServerTest, PostCID) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(Request::Method::POST, "/cid", Node("cookie"));
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Created);
  EXPECT_EQ(request.response_location->encode(), "/cid/uAXEAB2Zjb29raWU");
}

TEST(ServerTest, PostCIDLarge) {
  Node node(node_list_arg);
  for (size_t i = 0; i < 1024; ++i)
    node.push_back(i);
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(Request::Method::POST, "/cid", node);
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Created);
  EXPECT_EQ(request.response_location->encode(),
            "/cid/uAXGg5AIg6aa9gvagXHAJtTCI5l_QXWbIMNnQN6905en1kSnHNPo");
  EXPECT_EQ(evaluator.getStore().get(*CID::parse(
                "uAXGg5AIg6aa9gvagXHAJtTCI5l_QXWbIMNnQN6905en1kSnHNPo")),
            node);
}

TEST(ServerTest, ListHeadsEmpty) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(Request::Method::GET, "/head");
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Content);
  EXPECT_EQ(request.response_content_node, Node(node_map_arg));
  EXPECT_EQ(request.response_content_cid, std::nullopt);
  EXPECT_EQ(request.response_cache_control, Request::CacheControl::Mutable);
}

TEST(ServerTest, ListHeads) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  evaluator.getStore().set(Head("cookie"), *CID::parse("uAXEAB2Zjb29raWU"));
  evaluator.getStore().set(Head("empty"), *CID::parse("uAXEAAaA"));
  TestRequest request(Request::Method::GET, "/head");
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Content);
  EXPECT_EQ(request.response_content_node,
            Node(node_map_arg, {
                                   {"cookie", *CID::parse("uAXEAB2Zjb29raWU")},
                                   {"empty", *CID::parse("uAXEAAaA")},
                               }));
  EXPECT_EQ(request.response_content_cid, std::nullopt);
  EXPECT_EQ(request.response_cache_control, Request::CacheControl::Mutable);
}

TEST(ServerTest, GetHead) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  evaluator.getStore().set(Head("cookie"), *CID::parse("uAXEAB2Zjb29raWU"));
  TestRequest request(Request::Method::GET, "/head/cookie");
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Content);
  EXPECT_EQ(request.response_content_node,
            Node(*CID::parse("uAXEAB2Zjb29raWU")));
  EXPECT_EQ(request.response_content_cid, std::nullopt);
  EXPECT_EQ(request.response_cache_control, Request::CacheControl::Mutable);
}

TEST(ServerTest, PutHead) {
  Evaluator evaluator(Store::open("sqlite:test?mode=memory", true));
  Server server(evaluator);
  TestRequest request(Request::Method::PUT, "/head/cookie",
                      Node(*CID::parse("uAXEAB2Zjb29raWU")));
  server.handleRequest(request);
  ASSERT_EQ(request.response_type, ResponseType::Created);
  EXPECT_EQ(request.response_location, std::nullopt);
  EXPECT_EQ(evaluator.getStore().resolve(Head("cookie")),
            CID::parse("uAXEAB2Zjb29raWU"));
}

} // end anonymous namespace
