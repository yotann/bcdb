#ifndef MEMODB_SERVER_H
#define MEMODB_SERVER_H

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <memory>
#include <mutex>
#include <optional>

#include "CID.h"
#include "Evaluator.h"
#include "Node.h"
#include "Store.h"
#include "URI.h"

namespace memodb {

class DeferredRequestInfo;

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

  // All Request objects start in the New state, and eventually reach the Done
  // or Cancelled state. Allowed transitions are as follows:
  //
  // New -> Done
  // - The event loop calls Server::handleRequest(), which calls
  //   Request::send...().
  //
  // New -> Waiting
  // - The event loop calls Server::handleRequest(), which calls
  //   Request::deferWithTimeout().
  //
  // Waiting -> Done
  // - While handling a different Request, the Server calls Request::send...()
  //   on this Request.
  //
  // Waiting -> Cancelled
  // - The event loop detects that the client has disconnected. The event loop
  //   must call Server::handleRequest() after changing the state.
  //
  // Waiting -> TimedOut -> Done
  // - The event loop detects that the timeout given to
  //   Request::deferWithTimeout() has elapsed. The event loop must call
  //   Server::handleRequest() after changing the state to TimedOut, which in
  //   turn must call Request::send...().
  enum class State {
    New,
    Waiting,
    TimedOut,
    Cancelled,
    Done,
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

  virtual void sendContentURIs(const llvm::ArrayRef<URI> uris,
                               CacheControl cache_control);

  virtual void sendCreated(const std::optional<URI> &path) = 0;

  virtual void sendDeleted() = 0;

  virtual void sendError(Status status, std::optional<llvm::StringRef> type,
                         llvm::StringRef title,
                         const std::optional<llvm::Twine> &detail) = 0;

  virtual void sendMethodNotAllowed(llvm::StringRef allow) = 0;

  virtual void deferWithTimeout(unsigned seconds) = 0;

  // Must be locked before any member function is called or any other member
  // variable is accessed.
  std::mutex mutex;

  State state = State::New;

  DeferredRequestInfo *deferred_info = nullptr;
};

class Server {
public:
  Server(Evaluator &evaluator);

  // When this is called, request->mutex must already be locked. If the Server
  // calls Request::deferWithTimeout(), it may keep a copy of the shared_ptr
  // around indefinitely. This function will always cause request->state to
  // change (unless it's State::Cancelled).
  void handleRequest(const std::shared_ptr<Request> &request);

private:
  void handleNewRequest(Request &request);
  void handleRequestCID(Request &request,
                        std::optional<llvm::StringRef> cid_str);
  void handleRequestHead(Request &request,
                         std::optional<llvm::StringRef> head_str);
  void handleRequestCall(Request &request,
                         std::optional<llvm::StringRef> func_str,
                         std::optional<llvm::StringRef> args_str);

  Evaluator &evaluator;
  Store &store;
};

} // end namespace memodb

#endif // MEMODB_SERVER_H
