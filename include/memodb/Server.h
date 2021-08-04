#ifndef MEMODB_SERVER_H
#define MEMODB_SERVER_H

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>

#include "CID.h"
#include "Node.h"
#include "Store.h"
#include "URI.h"

namespace memodb {

class PendingCall;
class WorkerGroup;

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
  //
  // XXX: the Request must stay locked as long as the state is TimedOut!
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
    ServiceUnavailable = 503,
  };

  enum class CacheControl {
    Ephemeral,
    Mutable,
    Immutable,
  };

  virtual ~Request() {}
  virtual std::optional<Method> getMethod() const = 0;
  virtual std::optional<URI> getURI() const = 0;

  // Decode the Node that was submitted as the body of the request. If there's
  // no body, default_node should be returned if it is given; otherwise, or if
  // there's some other error reading the body, this function should send an
  // error response and return std::nullopt.
  virtual std::optional<Node>
  getContentNode(const std::optional<Node> &default_node = std::nullopt) = 0;

  virtual void sendContentNode(const Node &node,
                               const std::optional<CID> &cid_if_known,
                               CacheControl cache_control) = 0;

  virtual void sendContentURIs(const llvm::ArrayRef<URI> uris,
                               CacheControl cache_control);

  virtual void sendAccepted() = 0;

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

  PendingCall *pending_call = nullptr;
  WorkerGroup *worker_group = nullptr;
};

// Keeps track of all calls of a single function we need to evaluate. A
// CallGroup is never deleted, and has a fixed location in memory. All member
// variables and functions are protected by CallGroup::mutex.
class CallGroup {
public:
  std::mutex mutex;

  // The queue of calls we have been requested to evaluate that have not yet
  // been assigned to a worker. May include PendingCalls without any waiting
  // requests, if all requests have timed out, in which case the PendingCall
  // should be deleted as it is removed from the queue.
  std::deque<PendingCall *> unstarted_calls;

  // The actual PendingCalls.
  std::map<Call, PendingCall> calls;

  // All the WorkerGroups for workers that can evaluate this func.
  llvm::SmallVector<WorkerGroup *, 0> worker_groups;

  // Delete all PendingCalls from the start of unstarted_calls that have no
  // waiting requests.
  void deleteSomeUnstartedCalls();
};

// Keeps track of a single call we need to evaluate. A PendingCall can be
// deleted, and has a fixed location in memory. All member variables and
// functions, except read access to call_group and started, are protected by
// call_group->mutex.
class PendingCall {
public:
  CallGroup *call_group;
  Call call;

  // Whether the evaluation has been assigned to a worker. Needs to be atomic
  // so Server::handleTimeoutOrCancel can read it without a lock.
  std::atomic<bool> started = false;

  // The list of all requests that asked to evaluate this call. Some of these
  // requests may already have timed out. It is illegal to lock CallGroup::mutex
  // after locking one of these Requests' mutexes.
  llvm::SmallVector<std::shared_ptr<Request>, 1> requests;

  PendingCall(CallGroup *call_group, const Call &call)
      : call_group(call_group), call(call) {}

  // Delete this PendingCall from the parent CallGroup if possible.
  void deleteIfPossible();
};

// Keeps track of all workers with a single worker information CID. A
// WorkerGroup is never deleted, and has a fixed location in memory. All member
// variables and functions are protected by WorkerGroup::mutex, except
// call_groups (which is read-only after creation).
class WorkerGroup {
public:
  // It is illegal to lock CallGroup::mutex after locking this mutex.
  std::mutex mutex;

  // The queue of workers waiting for jobs. May include requests that have
  // already timed out. It is illegal to lock CallGroup::mutex or
  // WorkerGroup::mutex after locking one of these Requests' mutexes.
  std::deque<std::shared_ptr<Request>> workers;

  // All the CallGroups for funcs that these workers can handle.
  llvm::SmallVector<CallGroup *, 0> call_groups;
};

class Server {
public:
  Server(Store &store);

  // When this is called, request->mutex must already be locked. If the Server
  // calls Request::deferWithTimeout(), it may keep a copy of the shared_ptr
  // around indefinitely. This function will always cause request->state to
  // change (unless it's State::Cancelled).
  void handleRequest(const std::shared_ptr<Request> &request);

private:
  void handleNewRequest(const std::shared_ptr<Request> &request);
  void handleTimeoutOrCancel(const std::shared_ptr<Request> &request);
  void handleRequestCID(Request &request,
                        std::optional<llvm::StringRef> cid_str);
  void handleRequestHead(Request &request,
                         std::optional<llvm::StringRef> head_str);
  void handleRequestCall(const std::shared_ptr<Request> &request,
                         std::optional<llvm::StringRef> func_str,
                         std::optional<llvm::StringRef> args_str,
                         std::optional<llvm::StringRef> sub_str);
  void handleRequestWorker(const std::shared_ptr<Request> &request);
  void handleEvaluateCall(const std::shared_ptr<Request> &request, Call call,
                          unsigned timeout);
  void handleCallResult(const Call &call, NodeRef result);
  void sendCallToWorker(PendingCall &pending_call,
                        std::shared_ptr<Request> worker);

  Store &store;

  // All variables below are protected by the mutex.
  std::mutex mutex;
  llvm::StringMap<CallGroup> call_groups;
  llvm::StringMap<WorkerGroup> worker_groups;
};

} // end namespace memodb

#endif // MEMODB_SERVER_H
