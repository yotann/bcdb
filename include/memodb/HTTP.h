#ifndef MEMODB_HTTP_H
#define MEMODB_HTTP_H

#include <cstdint>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <optional>

#include "CID.h"
#include "Node.h"
#include "Server.h"

namespace memodb {

class Store;

class HTTPRequest : public Request {
public:
  std::optional<Node> getContentNode(
      Store &store,
      const std::optional<Node> &default_node = std::nullopt) override;

  ContentType chooseNodeContentType(const Node &node) override;

  bool sendETag(std::uint64_t etag, CacheControl cache_control) override;

  void sendContent(ContentType type, const llvm::StringRef &body) override;

  void sendAccepted() override;

  void sendCreated(const std::optional<URI> &path) override;

  void sendDeleted() override;

  void sendError(Status status, std::optional<llvm::StringRef> type,
                 llvm::StringRef title,
                 const std::optional<llvm::Twine> &detail) override;

  void sendMethodNotAllowed(llvm::StringRef allow) override;

protected:
  HTTPRequest(llvm::StringRef method_string, std::optional<URI> uri);

  // Should use case-insensitive comparison for key.
  // If more than one header matches, should return their values joined with
  // commas.
  virtual std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const = 0;

  virtual llvm::StringRef getBody() const = 0;

  virtual void sendStatus(std::uint16_t status) = 0;
  virtual void sendHeader(llvm::StringRef key, const llvm::Twine &value) = 0;

  // The implementation of this function must set the Content-Length header.
  // And if the request method is HEAD, it should ignore the body aside from
  // setting the Content-Length header.
  virtual void sendBody(const llvm::Twine &body) = 0;

  // Should set Content-Length header to 0.
  virtual void sendEmptyBody() = 0;

private:
  unsigned getAcceptQuality(ContentType content_type) const;

  bool hasIfNoneMatch(llvm::StringRef etag);

  void startResponse(std::uint16_t status, CacheControl cache_control);

  void sendErrorAfterStatus(Status status, std::optional<llvm::StringRef> type,
                            llvm::StringRef title,
                            const std::optional<llvm::Twine> &detail);
};

} // end namespace memodb

#endif // MEMODB_HTTP_H
