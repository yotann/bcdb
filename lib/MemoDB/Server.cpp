#include "memodb/Server.h"

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

Server::Server(Evaluator &evaluator)
    : evaluator(evaluator), store(evaluator.getStore()) {}

static bool isLegalUTF8(llvm::StringRef str) {
  auto source = reinterpret_cast<const llvm::UTF8 *>(str.data());
  auto sourceEnd = source + str.size();
  return llvm::isLegalUTF8String(&source, sourceEnd);
}

void Server::handleRequest(Request &request) {
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
    if (uri.path_segments.size() == 2)
      return handleRequestHead(request, uri.path_segments[1]);
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
      return request.sendContentNode(*cid, std::nullopt,
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
    Node result(node_map_arg);
    store.eachHead([&](const Head &head) {
      result[head.Name] = store.resolve(head);
      return false;
    });
    return request.sendContentNode(result, std::nullopt,
                                   Request::CacheControl::Mutable);
  }
}
