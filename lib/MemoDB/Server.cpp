#include "memodb/Server.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"

using namespace memodb;

Server::Server(Store &store) : store(store) {}

void Server::handleRequest(Request &request) {
  if (request.getMethod() == std::nullopt)
    return request.sendError(Request::Status::NotImplemented, std::nullopt,
                             "Not Implemented", std::nullopt);

  auto uri_or_null = request.getURI();
  if (!uri_or_null)
    return request.sendError(Request::Status::BadRequest, std::nullopt,
                             "Bad Request", std::nullopt);
  auto uri = std::move(*uri_or_null);

  if (uri.path_segments.size() >= 1 && uri.path_segments[0] == "cid") {
    if (uri.path_segments.size() == 1)
      return handleRequestCIDWithoutCID(request);
    if (uri.path_segments.size() == 2)
      return handleRequestCIDWithCID(request, uri.path_segments[1]);
  }

  return request.sendError(Request::Status::NotFound, std::nullopt, "Not Found",
                           std::nullopt);
}

void Server::handleRequestCIDWithoutCID(Request &request) {
  if (request.getMethod() != Request::Method::POST)
    return request.sendMethodNotAllowed("POST");
  // POST /cid
  auto node_or_null = request.getContentNode();
  if (!node_or_null)
    return;
  auto cid = store.put(*node_or_null);
  return request.sendCreated(llvm::StringRef("/cid/") +
                             cid.asString(Multibase::base64url));
}

void Server::handleRequestCIDWithCID(Request &request,
                                     llvm::StringRef cid_str) {
  if (request.getMethod() != Request::Method::GET)
    return request.sendMethodNotAllowed("GET, HEAD");
  // GET /cid/...
  auto cid = CID::parse(cid_str);
  if (!cid)
    return request.sendError(Request::Status::BadRequest,
                             "/problems/invalid-or-unsupported-cid",
                             "Invalid or unsupported CID",
                             "CID \"" + cid_str + "\" could not be parsed.");
  auto node = store.getOptional(*cid);
  if (!node)
    return request.sendError(Request::Status::NotFound, std::nullopt,
                             "Not Found",
                             "CID \"" + cid_str + "\" not found in store.");
  return request.sendContentNode(*node, cid, Request::CacheControl::Immutable);
}
