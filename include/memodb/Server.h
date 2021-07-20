#ifndef MEMODB_SERVER_H
#define MEMODB_SERVER_H

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <optional>

#include "CID.h"
#include "Evaluator.h"
#include "Node.h"
#include "Store.h"
#include "URI.h"

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
    UnsupportedMediaType = 415,
    NotImplemented = 501,
  };

  enum class CacheControl {
    Ephemeral,
    Mutable,
    Immutable,
  };

  virtual ~Request() {}
  virtual std::optional<Method> getMethod() const = 0;
  virtual std::optional<URI> getURI() const = 0;
  virtual std::optional<Node> getContentNode() = 0;

  virtual void sendContentNode(const Node &node,
                               const std::optional<CID> &cid_if_known,
                               CacheControl cache_control) = 0;

  virtual void sendCreated(const std::optional<URI> &path) = 0;

  virtual void sendError(Status status, std::optional<llvm::StringRef> type,
                         llvm::StringRef title,
                         const std::optional<llvm::Twine> &detail) = 0;

  virtual void sendMethodNotAllowed(llvm::StringRef allow) = 0;
};

class Server {
public:
  Server(Evaluator &evaluator);
  void handleRequest(Request &request);

private:
  void handleRequestCID(Request &request,
                        std::optional<llvm::StringRef> cid_str);
  void handleRequestHead(Request &request,
                         std::optional<llvm::StringRef> head_str);

  Evaluator &evaluator;
  Store &store;
};

} // end namespace memodb

#endif // MEMODB_SERVER_H
