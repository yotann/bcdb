#ifndef MEMODB_REQUEST_H
#define MEMODB_REQUEST_H

#include <cstdint>
#include <optional>
#include <string>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>

#include "CID.h"
#include "Node.h"
#include "Store.h"
#include "URI.h"

namespace memodb {

std::string escapeForHTML(llvm::StringRef str);

// A single request for the MemoDB server to respond to.
// This class is intended to work not only for HTTP requests but also CoAP
// requests, if a suitable subclass is implemented.
class Request {
public:
  enum class Method {
    GET,
    POST,
    PUT,
    DELETE,
  };

  /// One of the content types supported by the server.
  ///
  /// Numbers are based on the [CoAP
  /// Content-Formats](https://www.iana.org/assignments/core-parameters/core-parameters.xhtml#content-formats),
  /// using the "experimental use" range when appropriate.
  enum class ContentType : std::uint16_t {
    /// `text/plain;charset=utf-8`
    Plain = 0,
    /// `application/octet-stream`
    OctetStream = 42,
    /// `application/json`
    JSON = 50,
    /// `application/cbor`
    CBOR = 60,
    /// `text/html`
    HTML = 65000,
    /// `application/problem+json`
    ProblemJSON = 65001,
  };

  enum class Status {
    BadRequest = 400,
    NotFound = 404,
    MethodNotAllowed = 405,
    UnsupportedMediaType = 415,
    NotImplemented = 501,
    ServiceUnavailable = 503,
  };

  enum class CacheControl {
    Ephemeral,
    Mutable,
    Immutable,
  };

  Request(std::optional<Method> method, std::optional<URI> uri);
  virtual ~Request() {}

  // Decode the Node that was submitted as the body of the request. If there's
  // no body, default_node should be returned if it is given; otherwise, or if
  // there's some other error reading the body, this function should send an
  // error response and return std::nullopt.
  virtual std::optional<Node>
  getContentNode(Store &store,
                 const std::optional<Node> &default_node = std::nullopt) = 0;

  virtual ContentType chooseNodeContentType(const Node &node) = 0;

  // Returns true if no further response is necessary.
  virtual bool sendETag(std::uint64_t etag, CacheControl cache_control) = 0;

  virtual void sendContent(ContentType type, const llvm::StringRef &body) = 0;

  virtual void sendAccepted() = 0;

  virtual void sendCreated(const std::optional<URI> &path) = 0;

  virtual void sendDeleted() = 0;

  virtual void sendError(Status status, std::optional<llvm::StringRef> type,
                         llvm::StringRef title,
                         const std::optional<llvm::Twine> &detail) = 0;

  virtual void sendMethodNotAllowed(llvm::StringRef allow) = 0;

  virtual void sendContentNode(const Node &node,
                               const std::optional<CID> &cid_if_known,
                               CacheControl cache_control);

  virtual void sendContentURIs(const llvm::ArrayRef<URI> uris,
                               CacheControl cache_control);

  // The Request subclass should set this to true when any of the sendXXX
  // functions is called.
  bool responded = false;

  const std::optional<Method> method;
  const std::optional<URI> uri;
};

} // end namespace memodb

#endif // MEMODB_REQUEST_H
