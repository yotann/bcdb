#include "memodb/Server.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/ConvertUTF.h>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"
#include "memodb/URI.h"

static const unsigned REQUEST_TIMEOUT = 60;
static const unsigned DEFAULT_CALL_TIMEOUT = 600;

using namespace memodb;
using llvm::SmallVector;
using llvm::StringRef;

static bool isLegalUTF8(llvm::StringRef str) {
  auto source = reinterpret_cast<const llvm::UTF8 *>(str.data());
  auto sourceEnd = source + str.size();
  return llvm::isLegalUTF8String(&source, sourceEnd);
}

void CallGroup::deleteSomeUnstartedCalls() {
  while (!unstarted_calls.empty()) {
    PendingCall *pending_call = unstarted_calls.front();
    if (!pending_call->requests.empty())
      break;
    unstarted_calls.pop_front();
    calls.erase(pending_call->call);
  }
}

void PendingCall::deleteIfPossible() {
  if (!requests.empty())
    return;
  if (started) {
    call_group->calls.erase(call);
  } else {
    call_group->deleteSomeUnstartedCalls();
  }
}

void Request::sendContentURIs(const llvm::ArrayRef<URI> uris,
                              CacheControl cache_control) {
  Node node(node_list_arg);
  for (const URI &uri : uris)
    node.emplace_back(utf8_string_arg, uri.encode());
  std::sort(node.list_range().begin(), node.list_range().end());
  return sendContentNode(node, std::nullopt, cache_control);
}

Server::Server(Evaluator &evaluator)
    : evaluator(evaluator), store(evaluator.getStore()) {}

void Server::handleRequest(const std::shared_ptr<Request> &request) {
  switch (request->state) {
  case Request::State::New:
    handleNewRequest(request);
    assert(request->state == Request::State::Done ||
           request->state == Request::State::Waiting);
    break;
  case Request::State::TimedOut:
    handleTimeoutOrCancel(request);
    assert(request->state == Request::State::Done);
    break;
  case Request::State::Cancelled:
    handleTimeoutOrCancel(request);
    assert(request->state == Request::State::Cancelled);
    break;
  case Request::State::Waiting:
  case Request::State::Done:
    llvm_unreachable("impossible request state");
    break;
  }
}

void Server::handleNewRequest(const std::shared_ptr<Request> &request) {
  if (request->getMethod() == std::nullopt)
    return request->sendError(Request::Status::NotImplemented, std::nullopt,
                              "Not Implemented", std::nullopt);

  auto uri_or_null = request->getURI();
  if (!uri_or_null ||
      (uri_or_null->rootless && !uri_or_null->path_segments.empty()))
    return request->sendError(Request::Status::BadRequest, std::nullopt,
                              "Bad Request", std::nullopt);
  auto uri = std::move(*uri_or_null);

  if (uri.path_segments.size() >= 1 && uri.path_segments[0] == "cid") {
    if (uri.path_segments.size() == 1)
      return handleRequestCID(*request, std::nullopt);
    if (uri.path_segments.size() == 2)
      return handleRequestCID(*request, uri.path_segments[1]);
  }
  if (uri.path_segments.size() >= 1 && uri.path_segments[0] == "head") {
    if (uri.path_segments.size() == 1)
      return handleRequestHead(*request, std::nullopt);
    if (uri.path_segments.size() >= 2)
      return handleRequestHead(*request, uri.getPathString(1));
  }
  if (uri.path_segments.size() >= 1 && uri.path_segments[0] == "call") {
    if (uri.path_segments.size() == 1)
      return handleRequestCall(request, std::nullopt, std::nullopt,
                               std::nullopt);
    if (uri.path_segments.size() == 2)
      return handleRequestCall(request, uri.path_segments[1], std::nullopt,
                               std::nullopt);
    if (uri.path_segments.size() == 3)
      return handleRequestCall(request, uri.path_segments[1],
                               uri.path_segments[2], std::nullopt);
    if (uri.path_segments.size() == 4)
      return handleRequestCall(request, uri.path_segments[1],
                               uri.path_segments[2], uri.path_segments[3]);
  }
  if (uri.path_segments.size() == 2 && uri.path_segments[0] == "debug" &&
      uri.path_segments[1] == "timeout") {
    request->deferWithTimeout(2);
    return;
  }

  return request->sendError(Request::Status::NotFound, std::nullopt,
                            "Not Found", std::nullopt);
}

void Server::handleTimeoutOrCancel(const std::shared_ptr<Request> &request) {
  bool cancelled = request->state == Request::State::Cancelled;
  if (request->pending_call) {
    if (cancelled)
      return;
    auto pending_call = request->pending_call;
    if (pending_call->started)
      request->sendAccepted();
    else
      request->sendError(Request::Status::ServiceUnavailable, std::nullopt,
                         "Service Unavailable",
                         "No available workers can evaluate \"" +
                             pending_call->call.Name + "\"");

    // TODO: we need to eventually delete PendingCalls for which all requests
    // have timed out. Normally, the requesters will keep retrying and reusing
    // the PendingCall until eventually the evaluation is successful and
    // Server::handleCallRequest() deletes the PendingCall. However, if all
    // the requesters time out and they don't retry, the PendingCall will
    // never be deleted, causing a memory leak.
    //
    // It would be too awkward to handle that here without causing deadlocks.
    // Plus, usually we want to keep the PendingCall around so it can be
    // reused when the client retries. The best solution would be for each
    // call to Server::handleEvaluateCall() to incrementally walk through a
    // few PendingCalls, deleting them if they're no longer needed.
  } else {
    // /debug/timeout
    if (cancelled)
      return;
    request->sendContentNode("timed out", std::nullopt,
                             Request::CacheControl::Ephemeral);
  }
}

void Server::handleRequestCID(Request &request,
                              std::optional<llvm::StringRef> cid_str) {
  if (cid_str) {
    if (request.getMethod() != Request::Method::GET)
      return request.sendMethodNotAllowed("GET, HEAD");
    // GET /cid/...
    auto cid = CID::parse(*cid_str);
    if (!cid)
      return request.sendError(Request::Status::BadRequest,
                               "/problems/invalid-or-unsupported-cid",
                               "Invalid or unsupported CID",
                               "CID \"" + *cid_str + "\" could not be parsed.");
    auto node = store.getOptional(*cid);
    if (!node)
      return request.sendError(Request::Status::NotFound, std::nullopt,
                               "Not Found",
                               "CID \"" + *cid_str + "\" not found in store.");
    return request.sendContentNode(*node, cid,
                                   Request::CacheControl::Immutable);
  } else {
    if (request.getMethod() != Request::Method::POST)
      return request.sendMethodNotAllowed("POST");
    // POST /cid
    auto node_or_null = request.getContentNode();
    if (!node_or_null)
      return;
    auto cid = store.put(*node_or_null);
    URI result;
    result.path_segments.emplace_back("cid");
    result.path_segments.emplace_back(cid.asString(Multibase::base64url));
    return request.sendCreated(result);
  }
}

void Server::handleRequestHead(Request &request,
                               std::optional<llvm::StringRef> head_str) {
  if (head_str) {
    if (request.getMethod() == Request::Method::GET) {
      // GET /head/...
      auto cid = store.resolveOptional(Head(*head_str));
      if (!cid)
        return request.sendError(
            Request::Status::NotFound, std::nullopt, "Not Found",
            "Head \"" + *head_str + "\" not found in store.");
      return request.sendContentNode(Node(*cid), std::nullopt,
                                     Request::CacheControl::Mutable);
    } else if (request.getMethod() == Request::Method::PUT) {
      // PUT /head/...
      if (head_str->empty() || !isLegalUTF8(*head_str))
        return request.sendError(
            Request::Status::BadRequest, "/problems/invalid-string",
            "Invalid UTF-8 or unexpected empty string", std::nullopt);
      auto node_or_null = request.getContentNode();
      if (!node_or_null)
        return;
      if (!node_or_null->is<CID>())
        return request.sendError(
            Request::Status::BadRequest, "/problems/expected-cid",
            "Expected CID but got another kind of node", std::nullopt);
      store.set(Head(*head_str), node_or_null->as<CID>());
      return request.sendCreated(std::nullopt);
    } else {
      return request.sendMethodNotAllowed("GET, HEAD, PUT");
    }
  } else {
    if (request.getMethod() != Request::Method::GET)
      return request.sendMethodNotAllowed("GET, HEAD");
    // GET /head
    std::vector<URI> uris;
    store.eachHead([&](const Head &head) {
      uris.emplace_back();
      uris.back().path_segments = {"head", head.Name};
      uris.back().escape_slashes_in_segments = false;
      return false;
    });
    return request.sendContentURIs(uris, Request::CacheControl::Mutable);
  }
}

void Server::handleRequestCall(const std::shared_ptr<Request> &request,
                               std::optional<StringRef> func_str,
                               std::optional<StringRef> args_str,
                               std::optional<StringRef> sub_str) {
  if (func_str)
    if (func_str->empty() || !isLegalUTF8(*func_str))
      return request->sendError(
          Request::Status::BadRequest, "/problems/invalid-string",
          "Invalid UTF-8 or unexpected empty string", std::nullopt);

  std::optional<Call> call;
  if (func_str && args_str) {
    SmallVector<StringRef, 8> args_split;
    args_str->split(args_split, ',');
    call.emplace(*func_str, llvm::ArrayRef<CID>{});
    for (StringRef arg_str : args_split) {
      auto arg = CID::parse(arg_str);
      if (!arg)
        return request->sendError(
            Request::Status::BadRequest, "/problems/invalid-or-unsupported-cid",
            "Invalid or unsupported CID",
            "CID \"" + arg_str + "\" could not be parsed.");
      call->Args.emplace_back(std::move(*arg));
    }
  }

  if (sub_str) {
    if (sub_str != "evaluate")
      return request->sendError(Request::Status::NotFound, std::nullopt,
                                "Not Found", std::nullopt);
    if (request->getMethod() != Request::Method::POST)
      return request->sendMethodNotAllowed("POST");
    // POST /call/.../.../evaluate
    unsigned timeout = DEFAULT_CALL_TIMEOUT;
    auto body = request->getContentNode();
    if (body) {
      if (!body->is_map())
        return request->sendError(Request::Status::BadRequest, std::nullopt,
                                  "Invalid body kind", std::nullopt);
      if (body->count("timeout")) {
        if (!(*body)["timeout"].is<unsigned>())
          return request->sendError(Request::Status::BadRequest, std::nullopt,
                                    "Invalid body field: timeout",
                                    std::nullopt);
        timeout = (*body)["timeout"].as<unsigned>();
      }
    }
    return handleEvaluateCall(request, std::move(*call), timeout);

  } else if (args_str) {
    if (request->getMethod() != Request::Method::GET &&
        request->getMethod() != Request::Method::PUT)
      return request->sendMethodNotAllowed("GET, HEAD, PUT");

    if (request->getMethod() == Request::Method::GET) {
      // GET /call/.../...
      auto cid = store.resolveOptional(*call);
      if (!cid)
        return request->sendError(Request::Status::NotFound, std::nullopt,
                                  "Not Found", "Call not found in store.");
      return request->sendContentNode(Node(*cid), std::nullopt,
                                      Request::CacheControl::Mutable);
    } else {
      // PUT /call/.../...
      auto node_or_null = request->getContentNode();
      if (!node_or_null)
        return;
      if (!node_or_null->is<CID>())
        return request->sendError(
            Request::Status::BadRequest, "/problems/expected-cid",
            "Expected CID but got another kind of node", std::nullopt);
      store.set(*call, node_or_null->as<CID>());
      handleCallResult(*call, NodeRef(store, node_or_null->as<CID>()));
      return request->sendCreated(std::nullopt);
    }
  } else if (func_str) {
    if (request->getMethod() == Request::Method::GET) {
      // GET /call/...
      std::vector<URI> uris;
      store.eachCall(*func_str, [&](const Call &call) {
        std::string args;
        for (const CID &arg : call.Args)
          args += arg.asString(Multibase::base64url) + ",";
        args.pop_back();
        uris.emplace_back();
        uris.back().path_segments = {"call", func_str->str(), std::move(args)};
        return false;
      });
      return request->sendContentURIs(uris, Request::CacheControl::Mutable);
    } else if (request->getMethod() == Request::Method::DELETE) {
      // DELETE /call/...
      store.call_invalidate(*func_str);
      return request->sendDeleted();
    } else {
      return request->sendMethodNotAllowed("DELETE, GET, HEAD");
    }
  } else {
    if (request->getMethod() != Request::Method::GET)
      return request->sendMethodNotAllowed("GET, HEAD");

    // GET /call
    std::vector<URI> uris;
    for (const auto &func : store.list_funcs()) {
      uris.emplace_back();
      uris.back().path_segments = {"call", func};
    }
    return request->sendContentURIs(uris, Request::CacheControl::Mutable);
  }
}

void Server::handleEvaluateCall(const std::shared_ptr<Request> &request,
                                Call call, unsigned timeout) {
  // It's common for the result to already be in the store. Optimistically
  // check for that case before we bother locking any mutexes.
  auto result = store.resolveOptional(call);
  if (result) {
    request->sendContentNode(Node(*result), std::nullopt,
                             Request::CacheControl::Mutable);
    return;
  }

  std::unique_lock<std::mutex> lock(mutex);
  CallGroup &call_group = call_groups[call.Name];
  lock.unlock();
  lock = std::unique_lock<std::mutex>(call_group.mutex);

  // If the result has just been added to the store, return it now. If the
  // result still isn't in the store, we will respond to this request when it
  // gets added.
  result = store.resolveOptional(call);
  if (result) {
    request->sendContentNode(Node(*result), std::nullopt,
                             Request::CacheControl::Mutable);
    return;
  }

  // FIXME: check for queued workers that can handle this call.

  // TODO: if the pending_call has already been started, check whether the
  // worker has timed out.

  auto item = call_group.calls.try_emplace(call, &call_group, call);
  PendingCall &pending_call = item.first->second;
  if (item.second)
    call_group.unstarted_calls.push_back(&pending_call);
  // TODO: remove timed out requests from pending_call.requests.
  pending_call.requests.emplace_back(request);
  request->pending_call = &pending_call;
  request->deferWithTimeout(REQUEST_TIMEOUT);
}

void Server::handleCallResult(const Call &call, NodeRef result) {
  // Send the result to all waiting requests (if any).
  std::unique_lock<std::mutex> lock(mutex);
  auto call_group_it = call_groups.find(call.Name);
  if (call_group_it == call_groups.end())
    return;
  CallGroup &call_group = call_group_it->second;
  lock.unlock();
  lock = std::unique_lock<std::mutex>(call_group.mutex);
  auto pending_call_it = call_group.calls.find(call);
  if (pending_call_it == call_group.calls.end())
    return;
  PendingCall &pending_call = pending_call_it->second;
  for (auto &request : pending_call.requests) {
    std::lock_guard request_lock(request->mutex);
    assert(request->state != Request::State::New &&
           request->state != Request::State::TimedOut);
    if (request->state != Request::State::Waiting)
      continue;
    request->sendContentNode(Node(result.getCID()), std::nullopt,
                             Request::CacheControl::Mutable);
  }
  pending_call.requests.clear();
  pending_call.deleteIfPossible();
}
