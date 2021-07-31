#include "memodb/Server.h"

#include <algorithm>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/ConvertUTF.h>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"
#include "memodb/URI.h"

using namespace memodb;
using llvm::StringRef;

static bool isLegalUTF8(llvm::StringRef str) {
  auto source = reinterpret_cast<const llvm::UTF8 *>(str.data());
  auto sourceEnd = source + str.size();
  return llvm::isLegalUTF8String(&source, sourceEnd);
}

class memodb::DeferredRequestInfo {};

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
    handleNewRequest(*request);
    assert(request->state == Request::State::Done ||
           request->state == Request::State::Waiting);
    break;
  case Request::State::TimedOut:
    request->sendContentNode("timed out", std::nullopt,
                             Request::CacheControl::Ephemeral);
    assert(request->state == Request::State::Done);
    break;
  case Request::State::Cancelled:
    assert(request->state == Request::State::Cancelled);
    break;
  case Request::State::Waiting:
  case Request::State::Done:
    llvm_unreachable("impossible request state");
    break;
  }
}

void Server::handleNewRequest(Request &request) {
  if (request.getMethod() == std::nullopt)
    return request.sendError(Request::Status::NotImplemented, std::nullopt,
                             "Not Implemented", std::nullopt);

  auto uri_or_null = request.getURI();
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
      return handleRequestCall(request, std::nullopt, std::nullopt);
    if (uri.path_segments.size() == 2)
      return handleRequestCall(request, uri.path_segments[1], std::nullopt);
    if (uri.path_segments.size() == 3)
      return handleRequestCall(request, uri.path_segments[1],
                               uri.path_segments[2]);
  }
  if (uri.path_segments.size() == 2 && uri.path_segments[0] == "debug" &&
      uri.path_segments[1] == "timeout") {
    request.deferWithTimeout(2);
    return;
  }

  return request.sendError(Request::Status::NotFound, std::nullopt, "Not Found",
                           std::nullopt);
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

void Server::handleRequestCall(Request &request,
                               std::optional<StringRef> func_str,
                               std::optional<StringRef> args_str) {
  if (func_str && args_str) {

    if (request.getMethod() != Request::Method::GET &&
        request.getMethod() != Request::Method::PUT)
      return request.sendMethodNotAllowed("GET, HEAD, PUT");

    if (func_str->empty() || !isLegalUTF8(*func_str))
      return request.sendError(
          Request::Status::BadRequest, "/problems/invalid-string",
          "Invalid UTF-8 or unexpected empty string", std::nullopt);

    llvm::SmallVector<StringRef, 8> args_split;
    args_str->split(args_split, ',');
    Call call(*func_str, {});
    for (StringRef arg_str : args_split) {
      auto arg = CID::parse(arg_str);
      if (!arg)
        return request.sendError(
            Request::Status::BadRequest, "/problems/invalid-or-unsupported-cid",
            "Invalid or unsupported CID",
            "CID \"" + arg_str + "\" could not be parsed.");
      call.Args.emplace_back(std::move(*arg));
    }

    if (request.getMethod() == Request::Method::GET) {
      // GET /call/.../...
      auto cid = store.resolveOptional(call);
      if (!cid)
        return request.sendError(Request::Status::NotFound, std::nullopt,
                                 "Not Found", "Call not found in store.");
      return request.sendContentNode(Node(*cid), std::nullopt,
                                     Request::CacheControl::Mutable);
    } else {
      // PUT /call/.../...
      auto node_or_null = request.getContentNode();
      if (!node_or_null)
        return;
      if (!node_or_null->is<CID>())
        return request.sendError(
            Request::Status::BadRequest, "/problems/expected-cid",
            "Expected CID but got another kind of node", std::nullopt);
      store.set(call, node_or_null->as<CID>());
      return request.sendCreated(std::nullopt);
    }
  } else if (func_str) {

    if (func_str->empty() || !isLegalUTF8(*func_str))
      return request.sendError(
          Request::Status::BadRequest, "/problems/invalid-string",
          "Invalid UTF-8 or unexpected empty string", std::nullopt);

    if (request.getMethod() == Request::Method::GET) {
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
    } else if (request.getMethod() == Request::Method::DELETE) {
      // DELETE /call/...
      store.call_invalidate(*func_str);
      return request.sendDeleted();
    } else {
      return request.sendMethodNotAllowed("DELETE, GET, HEAD");
    }
  } else {
    if (request.getMethod() != Request::Method::GET)
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
