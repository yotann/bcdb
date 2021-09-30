#include "memodb/Server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/xxhash.h>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"
#include "memodb/URI.h"

static const unsigned DEFAULT_CALL_TIMEOUT = 600;

using namespace memodb;
using llvm::SmallVector;
using llvm::StringRef;
namespace chrono = std::chrono;
using std::chrono::steady_clock;

std::string memodb::escapeForHTML(llvm::StringRef str) {
  std::string escaped;
  std::size_t i = 0;
  while (i < str.size()) {
    std::size_t j = str.find_first_of("<\"", i);
    escaped += str.slice(i, j);
    if (j < str.size()) {
      escaped += str[j] == '<' ? "&lt;" : "&quot;";
      j += 1;
    }
    i = j;
  }
  return escaped;
}

static bool isLegalUTF8(llvm::StringRef str) {
  auto source = reinterpret_cast<const llvm::UTF8 *>(str.data());
  auto sourceEnd = source + str.size();
  return llvm::isLegalUTF8String(&source, sourceEnd);
}

void CallGroup::deleteSomeUnstartedCalls() {
  while (!unstarted_calls.empty()) {
    PendingCall *pending_call = unstarted_calls.front();
    if (!pending_call->finished)
      break;
    unstarted_calls.pop_front();
    calls.erase(pending_call->call);
  }
}

void PendingCall::deleteIfPossible() {
  if (started) {
    call_group->calls.erase(call);
  } else {
    call_group->deleteSomeUnstartedCalls();
  }
}

Request::Request(std::optional<Method> method, std::optional<URI> uri)
    : method(method), uri(uri) {}

void Request::sendContentNode(const Node &node,
                              const std::optional<CID> &cid_if_known,
                              CacheControl cache_control) {
  ContentType type = chooseNodeContentType(node);
  std::uint64_t etag = static_cast<std::uint16_t>(type);
  if (cid_if_known) {
    etag += llvm::xxHash64(cid_if_known->asBytes());
    if (sendETag(etag, cache_control))
      return;
  }

  llvm::StringRef body;
  std::vector<std::uint8_t> byte_buffer;
  llvm::SmallVector<char, 256> char_buffer;
  llvm::raw_svector_ostream stream(char_buffer);

  if (type == ContentType::OctetStream) {
    body = node.as<llvm::StringRef>(byte_string_arg);
  } else if (type == ContentType::CBOR) {
    byte_buffer = node.saveAsCBOR();
    body = llvm::StringRef(reinterpret_cast<const char *>(byte_buffer.data()),
                           byte_buffer.size());
  } else if (type == ContentType::HTML) {
    std::string cid_string = "MemoDB Node";
    if (cid_if_known)
      cid_string = cid_if_known->asString(Multibase::base64url);
    // Display JSON using jQuery json-viewer:
    // https://github.com/abodelot/jquery.json-viewer
    // Copy-and-paste should still work on the formatted JSON.
    //
    // react-json-view is another interesting option, but it can't easily be
    // used without recompiling it.
    //
    // Limitations:
    // - Integers larger than 53 bits will be converted to floats by
    //   JSON.parse().
    // - No special handling for MemoDB JSON types, like CIDs.
    llvm::SmallVector<char, 256> tmp_buffer;
    llvm::raw_svector_ostream tmp_stream(tmp_buffer);
    tmp_stream << node;
    stream << R"(<!DOCTYPE html>
<script src="https://unpkg.com/jquery@3.6/dist/jquery.min.js"></script>
<script src="https://unpkg.com/jquery.json-viewer@1.4/json-viewer/jquery.json-viewer.js"></script>
<link href="https://unpkg.com/jquery.json-viewer@1.4/json-viewer/jquery.json-viewer.css" type="text/css" rel="stylesheet">
<script>
  $(function() {
    $('pre').jsonViewer(JSON.parse($('pre').text()), {withQuotes:true});
  });
</script>
<title>)" << cid_string
           << "</title>\n<h1>" << cid_string << "</h1>\n<pre>"
           << escapeForHTML(tmp_stream.str()) << "</pre>\n";
    body = stream.str();
  } else if (type == ContentType::Plain) {
    if (node.is_link())
      stream << Name(node.as<CID>()) << "\n";
    else
      stream << node << "\n";
    body = stream.str();
  } else {
    type = ContentType::JSON; // in case a weird type was selected
    stream << node;
    body = stream.str();
  }

  if (!cid_if_known) {
    // Since the ETag is based on a non-cryptographic hash, it would be
    // possible for an attacker with write access to trick two different
    // caching proxies into thinking that two different cached bodies are
    // correct. But the attacker could accomplish the same thing, even with a
    // cryptographic hash, by constantly changing the stored value so each
    // proxy reads a different value, so it doesn't make much difference.
    etag += llvm::xxHash64(body);
    if (sendETag(etag, cache_control))
      return;
  }

  sendContent(type, body);
}

void Request::sendContentURIs(const llvm::ArrayRef<URI> uris,
                              CacheControl cache_control) {
  Node node(node_list_arg);
  for (const URI &uri : uris)
    node.emplace_back(utf8_string_arg, uri.encode());
  std::sort(node.list_range().begin(), node.list_range().end());
  auto type = chooseNodeContentType(node);
  if (type == ContentType::Plain) {
    llvm::SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream stream(buffer);
    for (const Node &item : node.list_range())
      stream << item.as<StringRef>() << "\n";

    std::uint64_t etag = static_cast<std::uint16_t>(type);
    etag += llvm::xxHash64(stream.str());
    if (sendETag(etag, cache_control))
      return;
    sendContent(type, stream.str());
    return;
  } else if (type == ContentType::HTML) {
    llvm::SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream stream(buffer);
    auto uri_str = escapeForHTML(uri->encode());
    stream << "<!DOCTYPE html>\n<title>" << uri_str << "</title>\n<h1>"
           << uri_str << "</h1>\n<ul>\n";
    for (const auto &item : node.list_range()) {
      auto str = escapeForHTML(item.as<StringRef>());
      stream << "<li><a href=\"" << str << "\">" << str << "</a></li>\n";
    }
    stream << "</ul>\n";

    std::uint64_t etag = static_cast<std::uint16_t>(type);
    etag += llvm::xxHash64(stream.str());
    if (sendETag(etag, cache_control))
      return;
    sendContent(type, stream.str());
    return;
  }

  // TODO: use the result of chooseURIsContentType() even when it isn't HTML.
  return sendContentNode(node, std::nullopt, cache_control);
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
      return handleRequestCID(request, std::nullopt);
    if (uri.path_segments.size() == 2)
      return handleRequestCID(request, uri.path_segments[1]);
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
                              std::optional<llvm::StringRef> cid_str) {
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
    auto node = store.getOptional(*cid);
    if (!node)
      return request.sendError(Request::Status::NotFound, std::nullopt,
                               "Not Found",
                               "CID \"" + *cid_str + "\" not found in store.");
    return request.sendContentNode(*node, cid,
                                   Request::CacheControl::Immutable);
  } else {
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
    unsigned timeout = DEFAULT_CALL_TIMEOUT;
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
    // TODO: time out unstarted calls if nothing has requested them recently.
    call_group->deleteSomeUnstartedCalls();
    if (call_group->unstarted_calls.empty())
      continue;
    PendingCall *pending_call = call_group->unstarted_calls.front();
    call_group->unstarted_calls.pop_front();
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
  } else if (pending_call.started) {
    // Print a warning if the job was started many minutes ago. Maybe the
    // worker crashed.
    auto minutes = chrono::floor<chrono::minutes>(steady_clock::now() -
                                                  pending_call.start_time);
    if (minutes.count() >= pending_call.minutes_to_report) {
      llvm::errs() << "Job in progress for " << minutes.count()
                   << " minutes: " << pending_call.call << "\n";
      pending_call.minutes_to_report *= 2;
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
  // TODO: add "timeout" key
  // TODO: add "uri" key
  Node node(node_map_arg,
            {
                {"func", Node(utf8_string_arg, pending_call.call.Name)},
                {"args", Node(node_list_arg)},
            });
  for (const CID &arg : pending_call.call.Args)
    node["args"].emplace_back(store, arg);
  worker.sendContentNode(node, std::nullopt, Request::CacheControl::Ephemeral);
  pending_call.started = true;
  pending_call.start_time = steady_clock::now();
  // Report long-running job after 4 minutes.
  pending_call.minutes_to_report = 4;
}
