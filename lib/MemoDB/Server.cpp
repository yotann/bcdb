#include "memodb/Server.h"

#include <cstdint>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <tiple>
#include <vector>

#include "memodb/CID.h"
#include "memodb/Node.h"

using namespace memodb;

Server::Server(Store &store) : store(store) {}

static void respondWithNode(const Request &request, Response &response,
                            const Node &node) {
  // Parse the Accept header, if any.
  // https://datatracker.ietf.org/doc/html/rfc7231#section-5.3.2
  // TODO: parse quoted-string properly.
  // TODO: validate format of q= parameter.
  double octet_stream_score = 0;
  double json_score = 0.0001;
  double cbor_score = 0;
  if (auto accept_val = request.getHeader("Accept")) {
    llvm::StringRef remainder = accept_val->trim(" \t");
    llvm::StringRef media_range, type, params, param;
    while (!remainder.empty()) {
      std::tie(media_range, remainder) = remainder.split(',');
      std::tie(type, params) = media_range.split(';');
      type = type.rtrim(" \t");
      double quality = 1.0;
      while (!params.empty()) {
        std::tie(param, params) = params.split(';');
        param = param.trim(" \t");
        if (param.startswith("q=")) {
          // Ignore errors.
          param.substr(2).getAsDouble(quality);
          break;
        }
      }
      if (type == "application/octet-stream")
        octet_stream_score = quality;
      else if (type == "application/json")
        json_score = quality;
      else if (type == "application/cbor")
        cbor_score = quality;
      remainder = remainder.ltrim(" \t");
    }
  }

  // TODO: add Cache-Control, ETag, and Server headers.

  if (node.kind() == Kind::Bytes && octet_stream_score >= json_score &&
      octet_stream_score >= cbor_score) {
    response.sendStatus(200);
    response.sendHeader("Content-Type", "application/octet-stream");
    response.sendBody(node.as<llvm::StringRef>(byte_string_arg));
    return;
  }

  if (cbor_score >= json_score) {
    std::vector<std::uint8_t> buffer;
    node.save_cbor(buffer);
    response.sendStatus(200);
    response.sendHeader("Content-Type", "application/cbor");
    response.sendBody(llvm::StringRef(
        reinterpret_cast<const char *>(buffer.data()), buffer.size()));
  } else {
    std::string buffer;
    llvm::raw_string_ostream(buffer) << node << "\n";
    response.sendStatus(200);
    response.sendHeader("Content-Type", "application/json");
    response.sendBody(buffer);
  }
}

void Server::handleRequest(const Request &request, Response &response) {
  auto uri = request.getURI();
  if (uri.startswith("/cid/")) {
    if (request.getMethod() != "GET") {
      // FIXME: allow HEAD.
      response.sendStatus(405);
      response.sendHeader("Allow", "GET");
      response.sendHeader("Content-Type", "text/plain");
      response.sendBody("Method Not Allowed\n");
      return;
    }
    auto cid = CID::parse(uri.substr(5));
    if (!cid) {
      response.sendStatus(404);
      response.sendHeader("Content-Type", "text/plain");
      response.sendBody("Invalid CID format.\n");
      return;
    }
    auto node = store.getOptional(*cid);
    if (!node) {
      response.sendStatus(404);
      response.sendHeader("Content-Type", "text/plain");
      response.sendBody("CID not found in store.\n");
      return;
    }
    respondWithNode(request, response, *node);
    return;
  }

  response.sendStatus(404);
  response.sendHeader("Content-Type", "text/plain");
  response.sendBody("Unknown path.\n");
}
