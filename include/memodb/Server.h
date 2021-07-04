#ifndef MEMODB_SERVER_H
#define MEMODB_SERVER_H

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <optional>

#include "CID.h"
#include "Node.h"
#include "Store.h"
#include "Support.h"

namespace memodb {

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

  enum class ContentType {
    OctetStream,
    JSON,
    CBOR,
    HTML,
    ProblemJSON,
  };

  enum class Status {
    BadRequest = 400,
    NotFound = 404,
    MethodNotAllowed = 405,
    NotImplemented = 501,
  };

  enum class CacheControl {
    Mutable,
    Immutable,
  };

  virtual ~Request() {}
  virtual std::optional<Method> getMethod() const = 0;
  virtual std::optional<ParsedURI> getURI() const = 0;
  virtual unsigned getAcceptQuality(ContentType content_type) const = 0;

  virtual void sendContentNode(const Node &node,
                               const std::optional<CID> &cid_if_known,
                               CacheControl cache_control) = 0;

  virtual void sendError(Status status, std::optional<llvm::StringRef> type,
                         llvm::StringRef title,
                         const std::optional<llvm::Twine> &detail) = 0;

  virtual void sendMethodNotAllowed(llvm::StringRef allow) = 0;
};

class Server {
public:
  Server(Store &store);
  void handleRequest(Request &request);

private:
  Store &store;
};

} // end namespace memodb

#endif // MEMODB_SERVER_H
