#include "memodb/HTTP.h"

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
using llvm::StringRef;

static std::string escapeForHTML(llvm::StringRef str) {
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

// Parse the Accept header and find the q= value (if any) for the specified
// content_type. Return the q= value scaled from 0 to 1000.
// https://datatracker.ietf.org/doc/html/rfc7231#section-5.3.2
unsigned HTTPRequest::getAcceptQuality(ContentType content_type) const {
  llvm::StringRef wanted_type, wanted_subtype;
  switch (content_type) {
  case ContentType::OctetStream:
    wanted_type = "application";
    wanted_subtype = "octet-stream";
    break;
  case ContentType::JSON:
    wanted_type = "application";
    wanted_subtype = "json";
    break;
  case ContentType::CBOR:
    wanted_type = "application";
    wanted_subtype = "cbor";
    break;
  case ContentType::HTML:
    wanted_type = "text";
    wanted_subtype = "html";
    break;
  case ContentType::ProblemJSON:
    wanted_type = "application";
    wanted_subtype = "problem+json";
    break;
  default:
    llvm_unreachable("impossible content_type");
  }

  auto accept = getHeader("Accept");
  if (!accept)
    return content_type == ContentType::JSON ||
                   content_type == ContentType::ProblemJSON
               ? 1
               : 0;
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

std::optional<Node>
HTTPRequest::getContentNode(Store &store,
                            const std::optional<Node> &default_node) {
  llvm::StringRef body_str = getBody();
  if (body_str.empty()) {
    if (default_node)
      return default_node;
    sendError(Status::BadRequest, "/problems/missing-body",
              "Missing request Body", std::nullopt);
    return std::nullopt;
  }

  auto type_str_or_null = getHeader("Content-Type");
  llvm::StringRef type_str =
      type_str_or_null ? type_str_or_null->split(';').first.trim(" \t") : "";

  if (type_str.equals_lower("application/cbor")) {
    BytesRef body_bytes(reinterpret_cast<const std::uint8_t *>(body_str.data()),
                        body_str.size());
    auto node_or_err = Node::loadFromCBOR(store, body_bytes);
    if (!node_or_err) {
      sendError(Status::BadRequest, "/problems/invalid-or-unsupported-cbor",
                "Invalid or unsupported CBOR",
                llvm::toString(node_or_err.takeError()));
      return std::nullopt;
    }
    return *node_or_err;

  } else if (type_str.equals_lower("application/octet-stream")) {
    return Node(byte_string_arg, body_str);

  } else if (type_str.equals_lower("application/json")) {
    auto node_or_err = Node::loadFromJSON(store, body_str);
    if (!node_or_err) {
      sendError(Status::BadRequest, "/problems/invalid-or-unsupported-json",
                "Invalid or unsupported JSON",
                llvm::toString(node_or_err.takeError()));
      return std::nullopt;
    }
    return *node_or_err;

  } else {
    sendError(Status::UnsupportedMediaType, std::nullopt,
              "Unsupported Media Type", std::nullopt);
    return std::nullopt;
  }
}

void HTTPRequest::sendError(Status status, std::optional<llvm::StringRef> type,
                            llvm::StringRef title,
                            const std::optional<llvm::Twine> &detail) {
  startResponse(static_cast<std::uint16_t>(status), CacheControl::Ephemeral);
  return sendErrorAfterStatus(status, type, title, detail);
}

void HTTPRequest::sendMethodNotAllowed(llvm::StringRef allow) {
  startResponse(405, CacheControl::Mutable);
  sendHeader("Allow", allow);
  return sendErrorAfterStatus(Status::MethodNotAllowed, std::nullopt,
                              "Method Not Allowed", std::nullopt);
}

// Send a JSON error message following RFC 7807.
void HTTPRequest::sendErrorAfterStatus(
    Status status, std::optional<llvm::StringRef> type, llvm::StringRef title,
    const std::optional<llvm::Twine> &detail) {
  unsigned problem_json_quality = getAcceptQuality(ContentType::ProblemJSON);
  unsigned html_quality = getAcceptQuality(ContentType::HTML);

  sendHeader("Content-Language", "en");

  if (html_quality > problem_json_quality) {
    sendHeader("Content-Type", "text/html");
    auto title_html = escapeForHTML(title);
    sendBody("<title>" + llvm::Twine(title_html) + "</title><h1>Error " +
             llvm::Twine(static_cast<unsigned>(status)) + ": " +
             llvm::Twine(title_html) + "</h1><p>" + (detail ? *detail : "") +
             "\n");
  } else {
    // TODO: Allow type-specific extension attributes to be added.
    llvm::SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream os(buffer);
    llvm::json::OStream json(os);
    json.objectBegin();
    if (type)
      json.attribute("type", *type);
    json.attribute("title", title);
    json.attribute("status", static_cast<unsigned>(status));
    if (detail)
      json.attribute("detail", detail->str());
    json.objectEnd();

    sendHeader("Content-Type", "application/problem+json");
    sendBody(os.str());
  }
}

bool HTTPRequest::hasIfNoneMatch(llvm::StringRef etag) {
  auto if_none_match = getHeader("If-None-Match");
  if (!if_none_match)
    return false;
  llvm::StringRef remainder = *if_none_match;
  while (true) {
    remainder = remainder.ltrim(" \t");
    if (remainder.empty())
      break;
    if (remainder.startswith("*"))
      return true;
    if (remainder.startswith("W/"))
      remainder = remainder.drop_front(2);
    if (!remainder.startswith("\""))
      return false; // invalid header
    remainder = remainder.drop_front(1);
    if (!remainder.contains('"'))
      return false; // invalid header
    llvm::StringRef wanted_etag;
    std::tie(wanted_etag, remainder) = remainder.split('"');
    if (wanted_etag == etag)
      return true;
    remainder = remainder.ltrim(" \t");
    if (!remainder.startswith(","))
      return false;
    remainder = remainder.drop_front(1);
  }
  return false;
}

void HTTPRequest::startResponse(std::uint16_t status,
                                CacheControl cache_control) {
  sendStatus(status);
  sendHeader("Server", "MemoDB");
  sendHeader("Vary", "Accept, Accept-Encoding");

  // TODO: consider adding "public".
  switch (cache_control) {
  case CacheControl::Ephemeral:
    sendHeader("Cache-Control", "max-age=0, must-revalidate");
    break;
  case CacheControl::Mutable:
    // TODO: consider caching these.
    sendHeader("Cache-Control", "max-age=0, must-revalidate");
    break;
  case CacheControl::Immutable:
    sendHeader("Cache-Control", "max-age=604800, immutable");
    break;
  }
}

void HTTPRequest::sendContent(CacheControl cache_control, llvm::StringRef etag,
                              llvm::StringRef content_type,
                              const llvm::Twine &content) {
  startResponse(200, cache_control);
  sendHeader("Content-Type", content_type);
  sendHeader("ETag", "\"" + etag + "\"");
  sendBody(content);
}

void HTTPRequest::sendContentNode(const Node &node,
                                  const std::optional<CID> &cid_if_known,
                                  CacheControl cache_control) {
  unsigned octet_stream_quality = getAcceptQuality(ContentType::OctetStream);
  unsigned json_quality = getAcceptQuality(ContentType::JSON);
  unsigned cbor_quality = getAcceptQuality(ContentType::CBOR);
  unsigned html_quality = getAcceptQuality(ContentType::HTML);

  CID cid = cid_if_known ? *cid_if_known : node.saveAsIPLD().first;
  std::string etag = cid.asString(Multibase::base64url);

  // When the client doesn't specify a preference, we prefer
  // json > octet-stream > cbor > html. Note that many clients, like curl and
  // Python's requests module, send "Accept: */*" by default.
  ContentType type = ContentType::JSON;
  if (node.kind() == Kind::Bytes && octet_stream_quality > json_quality &&
      octet_stream_quality >= cbor_quality &&
      octet_stream_quality >= html_quality) {
    etag = "raw+" + etag;
    type = ContentType::OctetStream;
  } else if (html_quality > cbor_quality && html_quality > json_quality) {
    etag = "html+" + etag;
    type = ContentType::HTML;
  } else if (cbor_quality > json_quality) {
    etag = "cbor+" + etag;
    type = ContentType::CBOR;
  } else {
    etag = "json+" + etag;
    type = ContentType::JSON;
  }

  if (hasIfNoneMatch(etag)) {
    startResponse(304, cache_control);
    sendHeader("ETag", "\"" + etag + "\"");
    sendEmptyBody();
    return;
  }

  if (type == ContentType::OctetStream) {
    sendContent(cache_control, etag, "application/octet-stream",
                node.as<llvm::StringRef>(byte_string_arg));
  } else if (type == ContentType::HTML) {
    std::string cid_string = "MemoDB Node";
    if (cid_if_known)
      cid_string = cid_if_known->asString(Multibase::base64url);
    llvm::SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream stream(buffer);
    stream << node;

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
    sendContent(cache_control, etag, "text/html",
                R"(<!DOCTYPE html>
<script src="https://unpkg.com/jquery@3.6/dist/jquery.min.js"></script>
<script src="https://unpkg.com/jquery.json-viewer@1.4/json-viewer/jquery.json-viewer.js"></script>
<link href="https://unpkg.com/jquery.json-viewer@1.4/json-viewer/jquery.json-viewer.css" type="text/css" rel="stylesheet">
<script>
  $(function() {
    $('pre').jsonViewer(JSON.parse($('pre').text()), {withQuotes:true});
  });
</script>
<title>)" + llvm::StringRef(cid_string) +
                    "</title>\n<h1>" + llvm::StringRef(cid_string) +
                    "</h1>\n<pre>" + escapeForHTML(stream.str()) + "</pre>\n");
  } else if (type == ContentType::CBOR) {
    auto buffer = node.saveAsCBOR();
    sendContent(cache_control, etag, "application/cbor",
                llvm::StringRef(reinterpret_cast<const char *>(buffer.data()),
                                buffer.size()));
  } else {
    llvm::SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream stream(buffer);
    stream << node;
    sendContent(cache_control, etag, "application/json", stream.str());
  }
}

void HTTPRequest::sendContentURIs(const llvm::ArrayRef<URI> uris,
                                  CacheControl cache_control) {
  unsigned json_quality = getAcceptQuality(ContentType::JSON);
  unsigned cbor_quality = getAcceptQuality(ContentType::CBOR);
  unsigned html_quality = getAcceptQuality(ContentType::HTML);
  if (html_quality <= json_quality || html_quality <= cbor_quality)
    return Request::sendContentURIs(uris, cache_control);

  Node node(node_list_arg);
  for (const URI &uri : uris)
    node.emplace_back(utf8_string_arg, uri.encode());
  std::sort(node.list_range().begin(), node.list_range().end());

  CID cid = node.saveAsIPLD().first;
  std::string etag = cid.asString(Multibase::base64url);

  auto uri_str = getURI()->encode();

  std::string html = "<!DOCTYPE html>\n<title>" + escapeForHTML(uri_str) +
                     "</title>\n<h1>" + escapeForHTML(uri_str) +
                     "</h1>\n<ul>\n";
  for (const auto &item : node.list_range()) {
    auto str = escapeForHTML(item.as<StringRef>());
    html += "<li><a href=\"" + str + "\">" + str + "</a></li>\n";
  }
  html += "</ul>\n";

  sendContent(cache_control, "html+" + etag, "text/html", html);
}

void HTTPRequest::sendAccepted() {
  startResponse(202, CacheControl::Ephemeral);
  sendEmptyBody();
}

void HTTPRequest::sendCreated(const std::optional<URI> &path) {
  // TODO: send ETag.
  startResponse(201, CacheControl::Ephemeral);
  if (path) {
    assert(path->scheme.empty());
    assert(path->host.empty());
    assert(path->fragment.empty());
    assert(path->port == 0);
    assert(!path->rootless);
    sendHeader("Location", path->encode());
  }
  sendEmptyBody();
}

void HTTPRequest::sendDeleted() {
  startResponse(204, CacheControl::Ephemeral);
  sendEmptyBody();
}

std::optional<Request::Method> HTTPRequest::getMethod() const {
  auto str = getMethodString();
  if (str.equals_lower("GET") || str.equals_lower("HEAD"))
    return Method::GET;
  if (str.equals_lower("POST"))
    return Method::POST;
  if (str.equals_lower("PUT"))
    return Method::PUT;
  if (str.equals_lower("DELETE"))
    return Method::DELETE;
  return std::nullopt;
}
