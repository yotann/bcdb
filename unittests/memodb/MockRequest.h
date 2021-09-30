#ifndef MEMODB_MOCKREQUEST_H
#define MEMODB_MOCKREQUEST_H

#include <cstdint>
#include <optional>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>

#include "memodb/CID.h"
#include "memodb/Node.h"
#include "memodb/Request.h"
#include "memodb/Store.h"
#include "memodb/URI.h"
#include "gmock/gmock.h"

namespace memodb {

class MockRequest : public Request {
public:
  MockRequest(std::optional<Method> method = std::nullopt,
              std::optional<llvm::StringRef> uri = std::nullopt)
      : Request(method, uri ? URI::parse(*uri) : std::nullopt) {
    setWillByDefault();
  }

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
              (Status status, std::optional<llvm::StringRef> type,
               llvm::StringRef title, const std::optional<llvm::Twine> &detail),
              (override));
  MOCK_METHOD(void, sendMethodNotAllowed, (llvm::StringRef allow), (override));
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
        .WillByDefault([this](Status, std::optional<llvm::StringRef>,
                              llvm::StringRef,
                              const std::optional<llvm::Twine> &) {
          ASSERT_FALSE(responded);
          responded = true;
        });
    ON_CALL(*this, sendMethodNotAllowed).WillByDefault([this](llvm::StringRef) {
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
    EXPECT_CALL(*this, getContentNode).Times(0);
  }

  void expectGetContent(std::optional<Node> content_node) {
    EXPECT_CALL(*this, getContentNode)
        .WillOnce([=](Store &store, const std::optional<Node> &default_node) {
          return content_node ? content_node : default_node;
        });
  }
};

} // namespace memodb

#endif // MEMODB_MOCKREQUEST_H
