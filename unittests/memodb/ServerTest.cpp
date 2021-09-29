#include "memodb/Server.h"

#include <llvm/ADT/StringRef.h>
#include <memory>
#include <optional>
#include <string>

#include "FakeStore.h"
#include "memodb/CID.h"
#include "memodb/Node.h"
#include "memodb/Store.h"
#include "memodb/URI.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace memodb;
using llvm::ArrayRef;
using llvm::StringRef;
using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Property;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

namespace {

enum class ResponseType {
  Content,
  Created,
  Deleted,
  Error,
  MethodNotAllowed,
};

class MockRequest : public Request {
public:
  MOCK_METHOD(std::optional<Method>, getMethod, (), (const, override));
  MOCK_METHOD(std::optional<URI>, getURI, (), (const, override));
  MOCK_METHOD(std::optional<Node>, getContentNode,
              (Store & store, const std::optional<Node> &default_node),
              (override));
  MOCK_METHOD(ContentType, chooseNodeContentType, (const Node &node),
              (override));
  MOCK_METHOD(bool, sendETag, (std::uint64_t etag, CacheControl cache_control),
              (override));
  MOCK_METHOD(void, sendContent,
              (ContentType type, const llvm::StringRef &body), (override));
  MOCK_METHOD(void, sendAccepted, (), (override));
  MOCK_METHOD(void, sendCreated, (const std::optional<URI> &path), (override));
  MOCK_METHOD(void, sendDeleted, (), (override));
  MOCK_METHOD(void, sendError,
              (Status status, std::optional<StringRef> type, StringRef title,
               const std::optional<llvm::Twine> &detail),
              (override));
  MOCK_METHOD(void, sendMethodNotAllowed, (StringRef allow), (override));
  MOCK_METHOD(void, sendContentNode,
              (const Node &node, const std::optional<CID> &cid_if_known,
               CacheControl cache_control),
              (override));
  MOCK_METHOD(void, sendContentURIs,
              (const llvm::ArrayRef<URI> uris, CacheControl cache_control),
              (override));

  void setWillByDefault() {
    ON_CALL(*this, sendContent)
        .WillByDefault([this](ContentType type, const llvm::StringRef &body) {
          ASSERT_FALSE(responded);
          responded = true;
        });
    ON_CALL(*this, sendAccepted).WillByDefault([this]() {
      ASSERT_FALSE(responded);
      responded = true;
    });
    ON_CALL(*this, sendCreated)
        .WillByDefault([this](const std::optional<URI> &) {
          ASSERT_FALSE(responded);
          responded = true;
        });
    ON_CALL(*this, sendDeleted).WillByDefault([this]() {
      ASSERT_FALSE(responded);
      responded = true;
    });
    ON_CALL(*this, sendError)
        .WillByDefault([this](Status, std::optional<StringRef>, StringRef,
                              const std::optional<llvm::Twine> &) {
          ASSERT_FALSE(responded);
          responded = true;
        });
    ON_CALL(*this, sendMethodNotAllowed).WillByDefault([this](StringRef) {
      ASSERT_FALSE(responded);
      responded = true;
    });
    ON_CALL(*this, sendContentNode)
        .WillByDefault(
            [this](const Node &, const std::optional<CID> &, CacheControl) {
              ASSERT_FALSE(responded);
              responded = true;
            });
    ON_CALL(*this, sendContentURIs)
        .WillByDefault([this](const llvm::ArrayRef<URI>, CacheControl) {
          ASSERT_FALSE(responded);
          responded = true;
        });
  }

  void expectGets(Method method, StringRef uri_str) {
    setWillByDefault();
    EXPECT_CALL(*this, getMethod).WillRepeatedly(Return(method));
    EXPECT_CALL(*this, getURI).WillOnce(Return(URI::parse(uri_str)));
    EXPECT_CALL(*this, getContentNode).Times(0);
  }

  void expectGets(Method method, StringRef uri_str,
                  std::optional<Node> content_node) {
    setWillByDefault();
    EXPECT_CALL(*this, getMethod).WillRepeatedly(Return(method));
    EXPECT_CALL(*this, getURI).WillOnce(Return(URI::parse(uri_str)));
    EXPECT_CALL(*this, getContentNode)
        .WillOnce([=](Store &store, const std::optional<Node> &default_node) {
          return content_node ? content_node : default_node;
        });
  }
};

TEST(ServerTest, UnknownMethod) {
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.setWillByDefault();
  EXPECT_CALL(request, getMethod).WillOnce(Return(std::nullopt));
  EXPECT_CALL(request, sendError(Request::Status::NotImplemented, _,
                                 Eq("Not Implemented"), _))
      .Times(1);
  server.handleRequest(request);
}

TEST(ServerTest, MethodNotAllowed) {
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::DELETE, "/cid");
  EXPECT_CALL(request, sendMethodNotAllowed(Eq("POST"))).Times(1);
  server.handleRequest(request);
}

TEST(ServerTest, DotSegmentsInURI) {
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::GET, "/cid/./uAXEAB2Zjb29raWU");
  EXPECT_CALL(request,
              sendError(Request::Status::BadRequest, _, Eq("Bad Request"), _));
  server.handleRequest(request);
}

TEST(ServerTest, GetCID) {
  FakeStore store;
  store.put(Node("cookie"));
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::GET, "/cid/uAXEAB2Zjb29raWU");
  EXPECT_CALL(request, sendContentNode(Node("cookie"),
                                       Eq(*CID::parse("uAXEAB2Zjb29raWU")),
                                       Request::CacheControl::Immutable));
  server.handleRequest(request);
}

TEST(ServerTest, PostCID) {
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::POST, "/cid", Node("cookie"));
  EXPECT_CALL(request, sendCreated(Eq(URI::parse("/cid/uAXEAB2Zjb29raWU"))));
  server.handleRequest(request);
}

TEST(ServerTest, PostCIDLarge) {
  Node node(node_list_arg);
  for (size_t i = 0; i < 1024; ++i)
    node.push_back(i);
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::POST, "/cid", node);
  EXPECT_CALL(
      request,
      sendCreated(Eq(URI::parse(
          "/cid/uAXGg5AIg6aa9gvagXHAJtTCI5l_QXWbIMNnQN6905en1kSnHNPo"))));
  server.handleRequest(request);
  EXPECT_EQ(store.get(*CID::parse(
                "uAXGg5AIg6aa9gvagXHAJtTCI5l_QXWbIMNnQN6905en1kSnHNPo")),
            node);
}

TEST(ServerTest, ListHeadsEmpty) {
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::GET, "/head");
  EXPECT_CALL(request,
              sendContentURIs(IsEmpty(), Request::CacheControl::Mutable));
  server.handleRequest(request);
}

TEST(ServerTest, ListHeads) {
  FakeStore store;
  Server server(store);
  store.set(Head("cookie"), *CID::parse("uAXEAB2Zjb29raWU"));
  store.set(Head("empty"), *CID::parse("uAXEAAaA"));
  MockRequest request;
  request.expectGets(Request::Method::GET, "/head");
  EXPECT_CALL(request,
              sendContentURIs(
                  Property(&ArrayRef<URI>::vec,
                           UnorderedElementsAre(*URI::parse("/head/cookie"),
                                                *URI::parse("/head/empty"))),
                  Request::CacheControl::Mutable));
  server.handleRequest(request);
}

TEST(ServerTest, GetHead) {
  FakeStore store;
  Server server(store);
  store.set(Head("cookie"), *CID::parse("uAXEAB2Zjb29raWU"));
  MockRequest request;
  request.expectGets(Request::Method::GET, "/head/cookie");
  EXPECT_CALL(request, sendContentNode(
                           Node(store, *CID::parse("uAXEAB2Zjb29raWU")),
                           Eq(std::nullopt), Request::CacheControl::Mutable));
  server.handleRequest(request);
}

TEST(ServerTest, PutHead) {
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::PUT, "/head/cookie",
                     Node(store, *CID::parse("uAXEAB2Zjb29raWU")));
  EXPECT_CALL(request, sendCreated(Eq(std::nullopt)));
  server.handleRequest(request);
  EXPECT_EQ(store.resolve(Head("cookie")), CID::parse("uAXEAB2Zjb29raWU"));
}

TEST(ServerTest, ListFuncs) {
  const CID cookie_cid = *CID::parse("uAXEAB2Zjb29raWU");
  const CID empty_cid = *CID::parse("uAXEAAaA");
  FakeStore store;
  Server server(store);
  store.set(Call("identity", {cookie_cid}), cookie_cid);
  store.set(Call("identity", {empty_cid}), empty_cid);
  store.set(Call("const_empty", {cookie_cid}), empty_cid);
  MockRequest request;
  request.expectGets(Request::Method::GET, "/call");
  EXPECT_CALL(request,
              sendContentURIs(Property(&ArrayRef<URI>::vec,
                                       UnorderedElementsAre(
                                           *URI::parse("/call/const_empty"),
                                           *URI::parse("/call/identity"))),
                              Request::CacheControl::Mutable));
  server.handleRequest(request);
}

TEST(ServerTest, InvalidateFunc) {
  const CID cookie_cid = *CID::parse("uAXEAB2Zjb29raWU");
  const CID empty_cid = *CID::parse("uAXEAAaA");
  FakeStore store;
  Server server(store);
  store.set(Call("identity", {cookie_cid}), cookie_cid);
  store.set(Call("identity", {empty_cid}), empty_cid);
  store.set(Call("const_empty", {cookie_cid}), empty_cid);
  MockRequest request;
  request.expectGets(Request::Method::DELETE, "/call/identity");
  EXPECT_CALL(request, sendDeleted());
  server.handleRequest(request);
  EXPECT_TRUE(!store.resolveOptional(Call("identity", {cookie_cid})));
  EXPECT_TRUE(!store.resolveOptional(Call("identity", {empty_cid})));
  EXPECT_EQ(store.resolveOptional(Call("const_empty", {cookie_cid})),
            empty_cid);
}

TEST(ServerTest, ListCalls) {
  const CID cookie_cid = *CID::parse("uAXEAB2Zjb29raWU");
  const CID empty_cid = *CID::parse("uAXEAAaA");
  FakeStore store;
  Server server(store);
  store.set(Call("transmute", {empty_cid, empty_cid}), cookie_cid);
  store.set(Call("transmute", {cookie_cid}), empty_cid);
  store.set(Call("const_empty", {cookie_cid}), empty_cid);
  MockRequest request;
  request.expectGets(Request::Method::GET, "/call/transmute");
  EXPECT_CALL(
      request,
      sendContentURIs(
          Property(&ArrayRef<URI>::vec,
                   UnorderedElementsAre(
                       *URI::parse("/call/transmute/uAXEAAaA,uAXEAAaA"),
                       *URI::parse("/call/transmute/uAXEAB2Zjb29raWU"))),
          Request::CacheControl::Mutable));
  server.handleRequest(request);
}

TEST(ServerTest, GetCall) {
  const CID cookie_cid = *CID::parse("uAXEAB2Zjb29raWU");
  const CID empty_cid = *CID::parse("uAXEAAaA");
  FakeStore store;
  Server server(store);
  store.set(Call("transmute", {empty_cid, empty_cid}), cookie_cid);
  MockRequest request;
  request.expectGets(Request::Method::GET, "/call/transmute/uAXEAAaA,uAXEAAaA");
  EXPECT_CALL(request,
              sendContentNode(Node(store, cookie_cid), Eq(std::nullopt),
                              Request::CacheControl::Mutable));
  server.handleRequest(request);
}

TEST(ServerTest, PutCall) {
  const CID cookie_cid = *CID::parse("uAXEAB2Zjb29raWU");
  const CID empty_cid = *CID::parse("uAXEAAaA");
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::PUT, "/call/transmute/uAXEAAaA,uAXEAAaA",
                     Node(store, cookie_cid));
  EXPECT_CALL(request, sendCreated(Eq(std::nullopt)));
  server.handleRequest(request);
  EXPECT_EQ(store.resolve(Call("transmute", {empty_cid, empty_cid})),
            cookie_cid);
}

TEST(ServerTest, EvaluateAccepted) {
  FakeStore store;
  Server server(store);
  MockRequest request;
  request.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                     std::nullopt);
  EXPECT_CALL(request, sendAccepted());
  server.handleRequest(request);
}

TEST(ServerTest, EvaluateCached) {
  FakeStore store;
  store.set(Call("inc", {*CID::parse("uAXEAAQA")}), *CID::parse("uAXEAAQE"));
  Server server(store);
  MockRequest evaluate_req;
  evaluate_req.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                          std::nullopt);
  EXPECT_CALL(evaluate_req,
              sendContentNode(Node(store, *CID::parse("uAXEAAQE")), _, _));
  server.handleRequest(evaluate_req);
}

TEST(ServerTest, EvaluateSuccessWithoutWorker) {
  FakeStore store;
  Server server(store);
  MockRequest evaluate_req;
  evaluate_req.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                          std::nullopt);
  EXPECT_CALL(evaluate_req, sendAccepted());
  server.handleRequest(evaluate_req);

  MockRequest put_req;
  put_req.expectGets(Request::Method::PUT, "/call/inc/uAXEAAQA",
                     Node(store, *CID::parse("uAXEAAQE")));
  EXPECT_CALL(put_req, sendCreated(Eq(std::nullopt)));
  server.handleRequest(put_req);

  MockRequest evaluate_req2;
  evaluate_req2.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                           std::nullopt);
  EXPECT_CALL(evaluate_req2,
              sendContentNode(Node(store, *CID::parse("uAXEAAQE")), _, _));
  server.handleRequest(evaluate_req2);
}

TEST(ServerTest, EvaluateMultiSuccessWithoutWorker) {
  FakeStore store;
  Server server(store);

  MockRequest evaluate0_req;
  evaluate0_req.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                           std::nullopt);
  EXPECT_CALL(evaluate0_req, sendAccepted());

  MockRequest evaluate1_req;
  evaluate1_req.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                           std::nullopt);
  EXPECT_CALL(evaluate1_req, sendAccepted());

  server.handleRequest(evaluate0_req);
  server.handleRequest(evaluate1_req);

  MockRequest put_req;
  put_req.expectGets(Request::Method::PUT, "/call/inc/uAXEAAQA",
                     Node(store, *CID::parse("uAXEAAQE")));
  EXPECT_CALL(put_req, sendCreated(Eq(std::nullopt)));
  server.handleRequest(put_req);
}

TEST(ServerTest, WorkerNoJobs) {
  FakeStore store;
  CID worker_cid = store.put(
      Node(node_map_arg, {{"funcs", Node(node_list_arg, {"id", "inc"})}}));
  Server server(store);

  MockRequest worker_req;
  worker_req.expectGets(Request::Method::POST, "/worker",
                        Node(store, worker_cid));
  EXPECT_CALL(worker_req, sendContentNode(Node(nullptr), _,
                                          Request::CacheControl::Ephemeral));

  server.handleRequest(worker_req);
}

TEST(ServerTest, WorkerBeforeEvaluate) {
  FakeStore store;
  CID worker_cid = store.put(
      Node(node_map_arg, {{"funcs", Node(node_list_arg, {"id", "inc"})}}));
  Server server(store);

  MockRequest worker_req;
  worker_req.expectGets(Request::Method::POST, "/worker",
                        Node(store, worker_cid));
  EXPECT_CALL(worker_req, sendContentNode(Node(nullptr), _,
                                          Request::CacheControl::Ephemeral));

  MockRequest evaluate_req;
  evaluate_req.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                          std::nullopt);
  EXPECT_CALL(evaluate_req, sendAccepted());

  server.handleRequest(worker_req);
  server.handleRequest(evaluate_req);
}

TEST(ServerTest, EvaluateBeforeWorker) {
  FakeStore store;
  CID worker_cid = store.put(
      Node(node_map_arg, {{"funcs", Node(node_list_arg, {"id", "inc"})}}));
  Server server(store);

  MockRequest evaluate_req;
  evaluate_req.expectGets(Request::Method::POST, "/call/inc/uAXEAAQA/evaluate",
                          std::nullopt);
  EXPECT_CALL(evaluate_req, sendAccepted());

  MockRequest worker_req;
  worker_req.expectGets(Request::Method::POST, "/worker",
                        Node(store, worker_cid));
  EXPECT_CALL(worker_req,
              sendContentNode(
                  Node(node_map_arg,
                       {{"args", Node(node_list_arg,
                                      {Node(store, *CID::parse("uAXEAAQA"))})},
                        {"func", "inc"}}),
                  _, Request::CacheControl::Ephemeral));

  MockRequest result_req;
  result_req.expectGets(Request::Method::PUT, "/call/inc/uAXEAAQA",
                        Node(store, *CID::parse("uAXEAAQE")));
  EXPECT_CALL(result_req, sendCreated(Eq(std::nullopt)));

  server.handleRequest(evaluate_req);
  server.handleRequest(worker_req);
  server.handleRequest(result_req);
}

// TODO: find a way to test interaction between threads.

} // end anonymous namespace
