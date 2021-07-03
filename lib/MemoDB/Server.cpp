#include "memodb/Server.h"

#include <cstdint>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"

using namespace memodb;

static std::string escapeForHTML(llvm::StringRef str) {
  std::string escaped;
  std::size_t i = 0;
  while (i < str.size()) {
    std::size_t j = str.find('<', i);
    escaped += str.slice(i, j);
    if (j < str.size()) {
      escaped += "&lt;";
      j += 1;
    }
    i = j;
  }
  return escaped;
}

Server::Server(Store &store) : store(store) {}

// Parse the Accept header and find the q= value (if any) for the specified
// content_type. Return the q= value scaled from 0 to 1000.
// https://datatracker.ietf.org/doc/html/rfc7231#section-5.3.2
unsigned Request::getAcceptQ(llvm::StringRef content_type) const {
  llvm::StringRef wanted_type, wanted_subtype;
  std::tie(wanted_type, wanted_subtype) = content_type.split('/');
  assert(!wanted_type.empty());
  assert(!wanted_subtype.empty());

  auto accept = getHeader("Accept");
  if (!accept)
    return 0;
  llvm::StringRef remainder = *accept;
  unsigned any_type_q = 0;
  std::optional<unsigned> any_subtype_q;

  while (true) {
    remainder = remainder.ltrim(" \t");
    if (remainder.empty())
      break;
    llvm::StringRef type;
    std::tie(type, remainder) = remainder.split('/');
    std::size_t i = remainder.find_first_of(";,");
    llvm::StringRef subtype = remainder.take_front(i).rtrim(" \t");
    remainder = remainder.substr(i);

    unsigned q = 1000;

    while (remainder.startswith(";")) {
      remainder = remainder.drop_front().ltrim(" \t");
      i = remainder.find_first_of(";,=");
      llvm::StringRef param = remainder.take_front(i).rtrim(" \t");
      remainder = remainder.substr(i);
      llvm::StringRef value;
      if (remainder.startswith("=")) {
        remainder = remainder.drop_front();
        if (remainder.startswith("\"")) {
          // skip quoted-string
          i = 1;
          while (i < remainder.size() && remainder[i] != '"') {
            i = remainder.find_first_of("\"\\", i);
            if (i != llvm::StringRef::npos && remainder[i] == '\\') {
              if (i + 1 == remainder.size())
                return 0; // missing end quote
              i += 2;
            }
          }
          if (i >= remainder.size())
            return 0; // missing end quote
          i++;
        } else {
          i = remainder.find_first_of(";,");
        }
        value = remainder.take_front(i).rtrim(" \t");
        remainder = remainder.substr(i).ltrim(" \t");
      }

      if (param.equals_lower("q") && value.size() >= 1) {
        char buffer[4] = {
            value[0],
            value.size() >= 3 && value[1] == '.' ? value[2] : '0',
            value.size() >= 4 && value[1] == '.' ? value[3] : '0',
            value.size() >= 5 && value[1] == '.' ? value[4] : '0',
        };
        // ignore error
        llvm::StringRef(buffer, 4).getAsInteger(10, q);
      }
    }

    if (type.equals_lower(wanted_type)) {
      if (subtype.equals_lower(wanted_subtype))
        return q;
      if (subtype == "*")
        any_subtype_q = q;
    } else if (type == "*" && subtype == "*") {
      any_type_q = q;
    }

    if (remainder.empty())
      break;
    if (!remainder.startswith(","))
      return 0; // extra characters after value
    remainder = remainder.drop_front();
  }

  if (any_subtype_q)
    return *any_subtype_q;
  return any_type_q;
}

void Response::sendStatus(std::uint16_t status) {
  assert(!status_sent);
  status_sent = true;
  sendStatusImpl(status);
}

void Response::sendHeader(const llvm::Twine &key, const llvm::Twine &value) {
  assert(status_sent);
  assert(!body_sent);
  sendHeaderImpl(key, value);
}

void Response::sendBody(const llvm::Twine &body) {
  assert(status_sent);
  assert(!body_sent);
  body_sent = true;
  sendBodyImpl(body);
}

// Send a JSON error message following RFC 7807.
void Response::sendError(std::uint16_t status,
                         std::optional<llvm::StringRef> type,
                         llvm::StringRef title,
                         const std::optional<llvm::Twine> &detail) {
  unsigned problem_json_score = request.getAcceptQ("application/problem+json");
  unsigned html_score = request.getAcceptQ("text/html");

  if (!status_sent)
    sendStatus(status);
  sendHeader("Content-Language", "en");

  if (html_score > problem_json_score) {
    sendHeader("Content-Type", "text/html");
    auto title_html = escapeForHTML(title);
    sendBody("<title>" + llvm::Twine(title_html) + "</title><h1>Error " +
             llvm::Twine(status) + ": " + llvm::Twine(title_html) + "</h1><p>" +
             (detail ? *detail : "") + "\n");
  } else {
    // TODO: Allow type-specific extension attributes to be added.
    llvm::SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream os(buffer);
    llvm::json::OStream json(os);
    json.objectBegin();
    if (type)
      json.attribute("type", *type);
    json.attribute("title", title);
    json.attribute("status", status);
    if (detail)
      json.attribute("detail", detail->str());
    json.objectEnd();

    sendHeader("Content-Type", "application/problem+json");
    sendBody(os.str());
  }
}

void Response::sendNode(const Node &node, const CID &cid) {
  unsigned octet_stream_score = request.getAcceptQ("application/octet-stream");
  unsigned json_score = request.getAcceptQ("application/json");
  unsigned cbor_score = request.getAcceptQ("application/cbor");
  unsigned html_score = request.getAcceptQ("text/html");

  // TODO: add Cache-Control, ETag, and Server headers.

  if (node.kind() == Kind::Bytes && octet_stream_score >= json_score &&
      octet_stream_score >= cbor_score && octet_stream_score >= html_score) {
    sendStatus(200);
    sendHeader("Content-Type", "application/octet-stream");
    sendBody(node.as<llvm::StringRef>(byte_string_arg));
    return;
  }

  if (html_score > cbor_score && html_score > json_score) {
    auto cid_string = cid.asString(Multibase::base64url);
    llvm::SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream stream(buffer);
    stream << node;

    sendStatus(200);
    sendHeader("Content-Type", "text/html");

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
    sendBody(R"(<!DOCTYPE html>
<script src="https://unpkg.com/jquery@3.6/dist/jquery.min.js"></script>
<script src="https://unpkg.com/jquery.json-viewer@1.4/json-viewer/jquery.json-viewer.js"></script>
<link href="https://unpkg.com/jquery.json-viewer@1.4/json-viewer/jquery.json-viewer.css" type="text/css" rel="stylesheet">
<script>
  $(function() {
    $('pre').jsonViewer(JSON.parse($('pre').text()), {withQuotes:true});
  });
</script>
<title>)" + llvm::StringRef(cid_string) +
             "</title>\n<h1>" + llvm::StringRef(cid_string) + "</h1>\n<pre>" +
             escapeForHTML(stream.str()) + "</pre>\n");
  } else if (cbor_score != 0 && cbor_score >= json_score) {
    std::vector<std::uint8_t> buffer;
    node.save_cbor(buffer);
    sendStatus(200);
    sendHeader("Content-Type", "application/cbor");
    sendBody(llvm::StringRef(reinterpret_cast<const char *>(buffer.data()),
                             buffer.size()));
  } else {
    std::string buffer;
    llvm::raw_string_ostream stream(buffer);
    stream << node << "\n";
    sendStatus(200);
    sendHeader("Content-Type", "application/json");
    sendBody(stream.str());
  }
}

void Server::handleRequest(const Request &request, Response &response) {
  auto uri = request.getURI();
  if (uri.startswith("/cid/")) {
    if (request.getMethod() != "GET" && request.getMethod() != "HEAD") {
      // FIXME: allow HEAD.
      response.sendStatus(405);
      response.sendHeader("Allow", "GET, HEAD");
      response.sendError(405, std::nullopt, "Method Not Allowed", std::nullopt);
      return;
    }
    auto cid = CID::parse(uri.substr(5));
    if (!cid) {
      response.sendError(400, "/problems/invalid-or-unsupported-cid",
                         "Invalid or unsupported CID.",
                         "CID \"" + uri.substr(5) + "\" could not be parsed.");
      return;
    }
    auto node = store.getOptional(*cid);
    if (!node) {
      response.sendError(404, std::nullopt, "Not Found",
                         "CID \"" + uri.substr(5) + "\" not found in store.");
      return;
    }
    response.sendNode(*node, *cid);
    return;
  }

  response.sendError(404, std::nullopt, "Not Found",
                     "Unknown path \"" + uri + "\".");
}
