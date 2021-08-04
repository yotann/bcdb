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
  if (uri.path_segments.size() == 1 && uri.path_segments[0] == "worker")
    return handleRequestWorker(request);
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
  } else if (request->worker_group) {
    // TODO: remove old workers from the group. We can't safely lock
    // request->worker_group->mutex, but we can use std::try_to_lock.
    if (cancelled)
      return;
    request->sendContentNode(nullptr, std::nullopt,
                             Request::CacheControl::Ephemeral);
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
    auto body = request->getContentNode(Node(node_map_arg));
    if (!body)
      return;
    if (!body->is_map())
      return request->sendError(Request::Status::BadRequest, std::nullopt,
                                "Invalid body kind", std::nullopt);
    if (body->count("timeout")) {
      if (!(*body)["timeout"].is<unsigned>())
        return request->sendError(Request::Status::BadRequest, std::nullopt,
                                  "Invalid body field: timeout", std::nullopt);
      timeout = (*body)["timeout"].as<unsigned>();
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

void Server::handleRequestWorker(const std::shared_ptr<Request> &request) {
  if (request->getMethod() != Request::Method::POST)
    return request->sendMethodNotAllowed("POST");
  // POST /worker
  auto node_or_null = request->getContentNode();
  if (!node_or_null)
    return;
  if (!node_or_null->is<CID>())
    return request->sendError(
        Request::Status::BadRequest, "/problems/expected-cid",
        "Expected CID but got another kind of node", std::nullopt);
  CID cid = node_or_null->as<CID>();
  auto cid_bytes = cid.asBytes();
  StringRef key(reinterpret_cast<const char *>(cid_bytes.data()),
                cid_bytes.size());

  // No deadlock: the only other mutex we hold is request->mutex, which isn't
  // accessible by other threads.
  std::unique_lock<std::mutex> lock(mutex);
  auto iter = worker_groups.find(key);
  WorkerGroup *worker_group;
  if (iter == worker_groups.end()) {
    auto info_or_null = store.getOptional(cid);
    if (!info_or_null)
      return request->sendError(
          Request::Status::BadRequest, "/problems/unknown-cid",
          "Provided CID was missing from the store", std::nullopt);
    if (!info_or_null->is_map() || !info_or_null->contains("funcs") ||
        !(*info_or_null)["funcs"].is_list())
      return request->sendError(
          Request::Status::BadRequest, "/problems/invalid-worker-info",
          "Provided worker info is invalid", std::nullopt);
    worker_group = &worker_groups[key];
    // No deadlock: worker_group.mutex is inaccessible to other threads (they
    // could only reach it by going through worker_groups, but that's protected
    // by Server::mutex, which we currently hold).
    std::unique_lock wg_lock(worker_group->mutex);
    for (const Node &func : (*info_or_null)["funcs"].list_range()) {
      if (!func.is<StringRef>())
        continue;
      // This may create a new CallGroup.
      CallGroup &call_group = call_groups[func.as<StringRef>()];
      worker_group->call_groups.emplace_back(&call_group);
    }
    wg_lock.unlock();
    lock.unlock();
    for (CallGroup *call_group : worker_group->call_groups) {
      // No deadlock: the only other mutex we hold is request->mutex, which
      // isn't accessible by other threads.
      std::lock_guard cg_lock(call_group->mutex);
      call_group->worker_groups.emplace_back(worker_group);
    }
  } else {
    worker_group = &iter->getValue();
    lock.unlock();
  }

  for (CallGroup *call_group : worker_group->call_groups) {
    // No deadlock: the only other mutex we hold is request->mutex, which isn't
    // accessible by other threads.
    std::lock_guard<std::mutex> cg_lock(call_group->mutex);
    call_group->deleteSomeUnstartedCalls();
    if (call_group->unstarted_calls.empty())
      continue;
    PendingCall *pending_call = call_group->unstarted_calls.front();
    call_group->unstarted_calls.pop_front();
    // XXX: this sends the call to the worker that just made a request,
    // ignoring other workers that may still be waiting.
    sendCallToWorker(*pending_call, request);
    return;
  }

  // No PendingCall found.
  // No deadlock: the only other mutex we hold is request->mutex, which isn't
  // accessible by other threads.
  lock = std::unique_lock(worker_group->mutex);
  worker_group->workers.emplace_back(request);
  request->worker_group = worker_group;
  request->deferWithTimeout(REQUEST_TIMEOUT);
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

  // No deadlock: the only other mutex we hold is request->mutex, which isn't
  // accessible by other threads.
  std::unique_lock<std::mutex> lock(mutex);
  CallGroup &call_group = call_groups[call.Name];
  lock.unlock();
  // No deadlock: the only other mutex we hold is request->mutex, which isn't
  // accessible by other threads.
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

  auto item = call_group.calls.try_emplace(call, &call_group, call);
  PendingCall &pending_call = item.first->second;
  // TODO: remove timed out requests from pending_call.requests.
  pending_call.requests.emplace_back(request);
  request->pending_call = &pending_call;
  request->deferWithTimeout(REQUEST_TIMEOUT);

  if (item.second) {
    // Check for queued workers that can handle this call.
    for (WorkerGroup *worker_group : call_group.worker_groups) {
      // No deadlock: the only other mutexes we hold are request->mutex, which
      // isn't accessible by other threads, and call_group.mutex, which can't
      // be locked by any thread that holds worker_group->mutex.
      std::lock_guard wg_lock(worker_group->mutex);
      while (!worker_group->workers.empty()) {
        std::shared_ptr<Request> worker =
            std::move(worker_group->workers.front());
        worker_group->workers.pop_front();
        // No deadlock: the only other mutexes we hold are request->mutex,
        // which isn't accessible by other threads, and call_group.mutex and
        // worker_group->mutex, which can't be locked by any thread that holds
        // worker->mutex.
        std::lock_guard lock(worker->mutex);
        if (worker->state != Request::State::Waiting)
          continue;
        sendCallToWorker(pending_call, std::move(worker));
        return;
      }
    }
    // No workers found.
    call_group.unstarted_calls.push_back(&pending_call);
  } else {
    // TODO: if the pending_call has already been started, check whether the
    // worker has timed out.
  }
}

void Server::handleCallResult(const Call &call, NodeRef result) {
  // Send the result to all waiting requests (if any).
  // No deadlock: the only other mutex we hold is the original request's mutex,
  // which isn't accessible by other threads.
  std::unique_lock<std::mutex> lock(mutex);
  auto call_group_it = call_groups.find(call.Name);
  if (call_group_it == call_groups.end())
    return;
  CallGroup &call_group = call_group_it->second;
  lock.unlock();
  // No deadlock: the only other mutex we hold is the original request's mutex,
  // which isn't accessible by other threads.
  lock = std::unique_lock<std::mutex>(call_group.mutex);
  auto pending_call_it = call_group.calls.find(call);
  if (pending_call_it == call_group.calls.end())
    return;
  PendingCall &pending_call = pending_call_it->second;
  for (auto &request : pending_call.requests) {
    // No deadlock: the only other mutexes we hold are the original request's
    // mutex, which isn't accessible by other threads, and call_group.mutex,
    // which can't be locked by any thread that holds request->mutex.
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

void Server::sendCallToWorker(PendingCall &pending_call,
                              std::shared_ptr<Request> worker) {
  assert(worker->state == Request::State::New ||
         worker->state == Request::State::Waiting);
  // TODO: add "timeout" key
  // TODO: add "uri" key
  Node node(node_map_arg,
            {
                {"func", Node(utf8_string_arg, pending_call.call.Name)},
                {"args", Node(node_list_arg)},
            });
  for (const CID &arg : pending_call.call.Args)
    node["args"].emplace_back(arg);
  worker->sendContentNode(node, std::nullopt, Request::CacheControl::Ephemeral);
  pending_call.started = true;
  // TODO: record the start time.
}
