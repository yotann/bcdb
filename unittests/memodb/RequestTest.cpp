#include "memodb/Request.h"

#include "FakeStore.h"
#include "MockRequest.h"
#include "TestingSupport.h"
#include "memodb/CID.h"
#include "memodb/Node.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace memodb;
using ::testing::Return;

namespace {

TEST(RequestTest, JSONDepth0) {
  FakeStore store;
  CID inner = store.put(2);
  CID middle = store.put(Node(store, inner));
  CID outer = store.put(Node(store, middle));
  MockRequest request(std::nullopt, "/test/?depth=0");
  EXPECT_CALL(request, chooseNodeContentType)
      .WillOnce(Return(Request::ContentType::JSON));
  EXPECT_CALL(
      request,
      sendContent(Request::ContentType::JSON,
                  TwineEq("{\"cid\":\"uAXEAEdgqTgABcQAJ2CpGAAFxAAEC\"}")));
  request.Request::sendContentNode(Node(store, outer), std::nullopt,
                                   Request::CacheControl::Ephemeral);
}

TEST(RequestTest, JSONDepth1) {
  FakeStore store;
  CID inner = store.put(2);
  CID middle = store.put(Node(store, inner));
  CID outer = store.put(Node(store, middle));
  MockRequest request(std::nullopt, "/test/?depth=1");
  EXPECT_CALL(request, chooseNodeContentType)
      .WillOnce(Return(Request::ContentType::JSON));
  EXPECT_CALL(
      request,
      sendContent(Request::ContentType::JSON,
                  TwineEq("{\"node\":{\"cid\":\"uAXEACdgqRgABcQABAg\"}}")));
  request.Request::sendContentNode(Node(store, outer), std::nullopt,
                                   Request::CacheControl::Ephemeral);
}

TEST(RequestTest, JSONDepth4) {
  FakeStore store;
  CID inner = store.put(2);
  CID middle = store.put(Node(store, inner));
  CID outer = store.put(Node(store, middle));
  MockRequest request(std::nullopt, "/test/?depth=4");
  EXPECT_CALL(request, chooseNodeContentType)
      .WillOnce(Return(Request::ContentType::JSON));
  EXPECT_CALL(request,
              sendContent(Request::ContentType::JSON,
                          TwineEq("{\"node\":{\"node\":{\"node\":2}}}")));
  request.Request::sendContentNode(Node(store, outer), std::nullopt,
                                   Request::CacheControl::Ephemeral);
}

} // end anonymous namespace
