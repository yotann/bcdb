#include "memodb/Server.h"

#include <chrono>
#include <deque>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ConvertUTF.h>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"
#include "memodb/Request.h"
#include "memodb/URI.h"

using namespace memodb;
using llvm::SmallVector;
using llvm::StringRef;
namespace chrono = std::chrono;
using std::chrono::steady_clock;

// Requeue a job if it has been assigned to a worker for this duration without
// a response. The job's timeout will increase exponentially each time it is
// requeued.
static const unsigned INITIAL_TIMEOUT_MINUTES = 4;

static bool isLegalUTF8(llvm::StringRef str) {
  auto source = reinterpret_cast<const llvm::UTF8 *>(str.data());
  auto sourceEnd = source + str.size();
  return llvm::isLegalUTF8String(&source, sourceEnd);
}

void CallGroup::deleteSomeFinishedCalls() {
  while (!unstarted_calls.empty()) {
    PendingCall *pending_call = unstarted_calls.front();
    if (!pending_call->finished)
      break;
    unstarted_calls.pop_front();
    calls.erase(pending_call->call);
  }
  while (!calls_to_retry.empty()) {
    PendingCall *pending_call = calls_to_retry.front();
    if (!pending_call->finished)
      break;
    calls_to_retry.pop_front();
    calls.erase(pending_call->call);
  }
}

PendingCall::PendingCall(CallGroup *call_group, const Call &call)
    : call_group(call_group), call(call),
      timeout_minutes(INITIAL_TIMEOUT_MINUTES) {}

void PendingCall::deleteIfPossible() {
  if (assigned) {
    call_group->calls.erase(call);
  } else {
    call_group->deleteSomeFinishedCalls();
  }
}

Server::Server(Store &store) : store(store) {}

void Server::handleRequest(Request &request) {
  handleNewRequest(request);
  assert(request.responded);
}

void Server::handleNewRequest(Request &request) {
  if (!request.method)
    return request.sendError(Request::Status::NotImplemented, std::nullopt,
                             "Not Implemented", std::nullopt);

  auto uri_or_null = request.uri;
  if (!uri_or_null ||
      (uri_or_null->rootless && !uri_or_null->path_segments.empty()))
    return request.sendError(Request::Status::BadRequest, std::nullopt,
                             "Bad Request", std::nullopt);
  auto uri = std::move(*uri_or_null);

  if (uri.path_segments.size() >= 1 && uri.path_segments[0] == "cid") {
    if (uri.path_segments.size() == 1)
      return handleRequestCID(request, std::nullopt, std::nullopt);
    if (uri.path_segments.size() == 2)
      return handleRequestCID(request, uri.path_segments[1], std::nullopt);
    if (uri.path_segments.size() == 3)
      return handleRequestCID(request, uri.path_segments[1],
                              uri.path_segments[2]);
  }
  if (uri.path_segments.size() >= 1 && uri.path_segments[0] == "head") {
    if (uri.path_segments.size() == 1)
      return handleRequestHead(request, std::nullopt);
    if (uri.path_segments.size() >= 2)
      return handleRequestHead(request, uri.getPathString(1));
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

  return request.sendError(Request::Status::NotFound, std::nullopt, "Not Found",
                           std::nullopt);
}

void Server::handleRequestCID(Request &request,
                              std::optional<llvm::StringRef> cid_str,
                              std::optional<llvm::StringRef> sub_str) {
  if (cid_str) {
    if (request.method != Request::Method::GET)
      return request.sendMethodNotAllowed("GET, HEAD");
    // GET /cid/...
    auto cid = CID::parse(*cid_str);
    if (!cid)
      return request.sendError(Request::Status::BadRequest,
                               "/problems/invalid-or-unsupported-cid",
                               "Invalid or unsupported CID",
                               "CID \"" + *cid_str + "\" could not be parsed.");
    if (sub_str && *sub_str == "users") {
      auto names = store.list_names_using(*cid);
      std::vector<URI> uris;
      for (const auto &name : names)
        uris.emplace_back(name.asURI());
      return request.sendContentURIs(uris, Request::CacheControl::Mutable);
    } else if (sub_str) {
      return request.sendError(Request::Status::NotFound, std::nullopt,
                               "Not Found", std::nullopt);
    }
    auto node = store.getOptional(*cid);
    if (!node)
      return request.sendError(Request::Status::NotFound, std::nullopt,
                               "Not Found",
                               "CID \"" + *cid_str + "\" not found in store.");
    return request.sendContentNode(*node, cid,
                                   Request::CacheControl::Immutable);
  } else {
    if (sub_str)
      return request.sendError(Request::Status::NotFound, std::nullopt,
                               "Not Found", std::nullopt);
    if (request.method != Request::Method::POST)
      return request.sendMethodNotAllowed("POST");
    // POST /cid
    auto node_or_null = request.getContentNode(store);
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
    if (request.method == Request::Method::GET) {
      // GET /head/...
      auto cid = store.resolveOptional(Head(*head_str));
      if (!cid)
        return request.sendError(
            Request::Status::NotFound, std::nullopt, "Not Found",
            "Head \"" + *head_str + "\" not found in store.");
      return request.sendContentNode(Node(store, *cid), std::nullopt,
                                     Request::CacheControl::Mutable);
    } else if (request.method == Request::Method::PUT) {
      // PUT /head/...
      if (head_str->empty() || !isLegalUTF8(*head_str))
        return request.sendError(
            Request::Status::BadRequest, "/problems/invalid-string",
            "Invalid UTF-8 or unexpected empty string", std::nullopt);
      auto node_or_null = request.getContentNode(store);
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
    if (request.method != Request::Method::GET)
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

void Server::handleRequestCall(Request &request,
                               std::optional<StringRef> func_str,
                               std::optional<StringRef> args_str,
                               std::optional<StringRef> sub_str) {
  if (func_str)
    if (func_str->empty() || !isLegalUTF8(*func_str))
      return request.sendError(
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
        return request.sendError(
            Request::Status::BadRequest, "/problems/invalid-or-unsupported-cid",
            "Invalid or unsupported CID",
            "CID \"" + arg_str + "\" could not be parsed.");
      call->Args.emplace_back(std::move(*arg));
    }
  }

  if (sub_str) {
    if (sub_str != "evaluate")
      return request.sendError(Request::Status::NotFound, std::nullopt,
                               "Not Found", std::nullopt);
    if (request.method != Request::Method::POST)
      return request.sendMethodNotAllowed("POST");
    // POST /call/.../.../evaluate
    unsigned timeout = 600;
    auto body = request.getContentNode(store, Node(node_map_arg));
    if (!body)
      return;
    if (!body->is_map())
      return request.sendError(Request::Status::BadRequest, std::nullopt,
                               "Invalid body kind", std::nullopt);
    if (body->count("timeout")) {
      if (!(*body)["timeout"].is<unsigned>())
        return request.sendError(Request::Status::BadRequest, std::nullopt,
                                 "Invalid body field: timeout", std::nullopt);
      timeout = (*body)["timeout"].as<unsigned>();
    }
    return handleEvaluateCall(request, std::move(*call), timeout);

  } else if (args_str) {
    if (request.method != Request::Method::GET &&
        request.method != Request::Method::PUT)
      return request.sendMethodNotAllowed("GET, HEAD, PUT");

    if (request.method == Request::Method::GET) {
      // GET /call/.../...
      auto cid = store.resolveOptional(*call);
      if (!cid)
        return request.sendError(Request::Status::NotFound, std::nullopt,
                                 "Not Found", "Call not found in store.");
      return request.sendContentNode(Node(store, *cid), std::nullopt,
                                     Request::CacheControl::Mutable);
    } else {
      // PUT /call/.../...
      auto node_or_null = request.getContentNode(store);
      if (!node_or_null)
        return;
      if (!node_or_null->is<CID>())
        return request.sendError(
            Request::Status::BadRequest, "/problems/expected-cid",
            "Expected CID but got another kind of node", std::nullopt);
      store.set(*call, node_or_null->as<CID>());
      handleCallResult(*call, Link(store, node_or_null->as<CID>()));
      return request.sendCreated(std::nullopt);
    }
  } else if (func_str) {
    if (request.method == Request::Method::GET) {
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
      return request.sendContentURIs(uris, Request::CacheControl::Mutable);
    } else if (request.method == Request::Method::DELETE) {
      // DELETE /call/...
      store.call_invalidate(*func_str);
      return request.sendDeleted();
    } else {
      return request.sendMethodNotAllowed("DELETE, GET, HEAD");
    }
  } else {
    if (request.method != Request::Method::GET)
      return request.sendMethodNotAllowed("GET, HEAD");

    // GET /call
    std::vector<URI> uris;
    for (const auto &func : store.list_funcs()) {
      uris.emplace_back();
      uris.back().path_segments = {"call", func};
    }
    return request.sendContentURIs(uris, Request::CacheControl::Mutable);
  }
}

void Server::handleRequestWorker(Request &request) {
  if (request.method != Request::Method::POST)
    return request.sendMethodNotAllowed("POST");
  // POST /worker
  auto node_or_null = request.getContentNode(store);
  if (!node_or_null)
    return;
  if (!node_or_null->is<CID>())
    return request.sendError(
        Request::Status::BadRequest, "/problems/expected-cid",
        "Expected CID but got another kind of node", std::nullopt);
  CID cid = node_or_null->as<CID>();
  auto cid_bytes = cid.asBytes();
  StringRef key(reinterpret_cast<const char *>(cid_bytes.data()),
                cid_bytes.size());

  // No deadlock: we don't hold any other mutexes.
  std::unique_lock<std::mutex> lock(mutex);
  auto iter = worker_groups.find(key);
  WorkerGroup *worker_group;
  if (iter == worker_groups.end()) {
    auto info_or_null = store.getOptional(cid);
    if (!info_or_null)
      return request.sendError(
          Request::Status::BadRequest, "/problems/unknown-cid",
          "Provided CID was missing from the store", std::nullopt);
    if (!info_or_null->is_map() || !info_or_null->contains("funcs") ||
        !(*info_or_null)["funcs"].is_list())
      return request.sendError(Request::Status::BadRequest,
                               "/problems/invalid-worker-info",
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
  } else {
    worker_group = &iter->getValue();
    lock.unlock();
  }

  for (CallGroup *call_group : worker_group->call_groups) {
    // No deadlock: we don't hold any other mutexes.
    std::lock_guard<std::mutex> cg_lock(call_group->mutex);
    call_group->deleteSomeFinishedCalls();
    if (call_group->unstarted_calls.empty())
      continue;
    PendingCall *pending_call = call_group->unstarted_calls.front();
    call_group->unstarted_calls.pop_front();
    sendCallToWorker(*pending_call, request);
    return;
  }

  for (CallGroup *call_group : worker_group->call_groups) {
    // No deadlock: we don't hold any other mutexes.
    std::lock_guard<std::mutex> cg_lock(call_group->mutex);
    call_group->deleteSomeFinishedCalls();
    if (call_group->calls_to_retry.empty())
      continue;
    PendingCall *pending_call = call_group->calls_to_retry.front();
    call_group->calls_to_retry.pop_front();
    sendCallToWorker(*pending_call, request);
    return;
  }

  // No PendingCall found.
  return request.sendContentNode(nullptr, std::nullopt,
                                 Request::CacheControl::Ephemeral);
}

void Server::handleEvaluateCall(Request &request, Call call, unsigned timeout) {
  // It's common for the result to already be in the store. Optimistically
  // check for that case before we bother locking any mutexes.
  auto result = store.resolveOptional(call);
  if (result) {
    request.sendContentNode(Node(store, *result), std::nullopt,
                            Request::CacheControl::Mutable);
    return;
  }

  // No deadlock: we don't hold any other mutexes.
  std::unique_lock<std::mutex> lock(mutex);
  CallGroup &call_group = call_groups[call.Name];
  lock.unlock();
  // No deadlock: we don't hold any other mutexes.
  lock = std::unique_lock<std::mutex>(call_group.mutex);

  // If the result has just been added to the store, return it now instead of
  // potentially creating a new PendingCall.
  result = store.resolveOptional(call);
  if (result) {
    request.sendContentNode(Node(store, *result), std::nullopt,
                            Request::CacheControl::Mutable);
    return;
  }

  auto item = call_group.calls.try_emplace(call, &call_group, call);
  PendingCall &pending_call = item.first->second;
  request.sendAccepted();

  if (item.second) {
    // New PendingCall, add it to the queue.
    call_group.unstarted_calls.push_back(&pending_call);
  } else if (pending_call.assigned) {
    // Print a warning and requeue the job if it was started many minutes ago.
    // Maybe the worker crashed.
    auto minutes = chrono::floor<chrono::minutes>(steady_clock::now() -
                                                  pending_call.start_time);
    if (minutes.count() >= pending_call.timeout_minutes) {
      llvm::errs() << "Job in progress for " << minutes.count()
                   << " minutes: " << pending_call.call
                   << "; queued for retry\n";
      // Exponentially increase timeout, to limit overhead if the old worker is
      // still running and this job is just really slow.
      pending_call.timeout_minutes *= 2;
      pending_call.assigned = false;
      call_group.calls_to_retry.push_back(&pending_call);
    }
  }
}

void Server::handleCallResult(const Call &call, Link result) {
  // Remove the PendingCall.
  // No deadlock: we don't hold any other mutexes.
  std::unique_lock<std::mutex> lock(mutex);
  auto call_group_it = call_groups.find(call.Name);
  if (call_group_it == call_groups.end())
    return;
  CallGroup &call_group = call_group_it->second;
  lock.unlock();
  // No deadlock: we don't hold any other mutexes.
  lock = std::unique_lock<std::mutex>(call_group.mutex);
  auto pending_call_it = call_group.calls.find(call);
  if (pending_call_it == call_group.calls.end())
    return;
  PendingCall &pending_call = pending_call_it->second;
  pending_call.finished = true;
  pending_call.deleteIfPossible();
}

void Server::sendCallToWorker(PendingCall &pending_call, Request &worker) {
  assert(!worker.responded);
  Node node(node_map_arg,
            {
                {"func", Node(utf8_string_arg, pending_call.call.Name)},
                {"args", Node(node_list_arg)},
            });
  for (const CID &arg : pending_call.call.Args)
    node["args"].emplace_back(store, arg);
  worker.sendContentNode(node, std::nullopt, Request::CacheControl::Ephemeral);
  pending_call.assigned = true;
  pending_call.start_time = steady_clock::now();
}
