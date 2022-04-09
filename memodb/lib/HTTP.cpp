#include "memodb/HTTP.h"

#include <cstdint>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/ScopedPrinter.h>
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

static bool equals_insensitive(llvm::StringRef lhs, llvm::StringRef rhs) {
#if LLVM_VERSION_MAJOR >= 13
  return lhs.equals_insensitive(rhs);
#else
  return lhs.equals_lower(rhs);
#endif
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

      if (equals_insensitive(param, "q") && value.size() >= 1) {
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

    if (equals_insensitive(type, wanted_type)) {
      if (equals_insensitive(subtype, wanted_subtype))
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

  if (equals_insensitive(type_str, "application/cbor")) {
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

  } else if (equals_insensitive(type_str, "application/octet-stream")) {
    return Node(byte_string_arg, body_str);

  } else if (equals_insensitive(type_str, "application/json")) {
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

Request::ContentType HTTPRequest::chooseNodeContentType(const Node &node) {
  unsigned octet_stream_quality = getAcceptQuality(ContentType::OctetStream);
  unsigned json_quality = getAcceptQuality(ContentType::JSON);
  unsigned cbor_quality = getAcceptQuality(ContentType::CBOR);
  unsigned html_quality = getAcceptQuality(ContentType::HTML);

  // When the client doesn't specify a preference, we prefer
  // json > octet-stream > cbor > html. Note that many clients, like curl and
  // Python's requests module, send "Accept: */*" by default.
  if (node.kind() == Kind::Bytes && octet_stream_quality > json_quality &&
      octet_stream_quality >= cbor_quality &&
      octet_stream_quality >= html_quality) {
    return ContentType::OctetStream;
  } else if (html_quality > cbor_quality && html_quality > json_quality) {
    return ContentType::HTML;
  } else if (cbor_quality > json_quality) {
    return ContentType::CBOR;
  } else {
    return ContentType::JSON;
  }
}

bool HTTPRequest::sendETag(std::uint64_t etag, CacheControl cache_control) {
  auto etag_str = llvm::to_hexString(etag, false);
  bool matched = hasIfNoneMatch(etag_str);
  startResponse(matched ? 304 : 200, cache_control);
  sendHeader("ETag", "\"" + etag_str + "\"");
  if (matched)
    sendEmptyBody();
  return matched;
}

void HTTPRequest::sendContent(ContentType type, const llvm::StringRef &body) {
  switch (type) {
  case ContentType::OctetStream:
    sendHeader("Content-Type", "application/octet-stream");
    break;
  case ContentType::HTML:
    sendHeader("Content-Type", "text/html");
    break;
  case ContentType::CBOR:
    sendHeader("Content-Type", "application/cbor");
    break;
  case ContentType::JSON:
    sendHeader("Content-Type", "application/json");
    break;
  case ContentType::ProblemJSON:
  case ContentType::Plain:
    llvm_unreachable("impossible content type");
  }
  sendBody(body);
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

static std::optional<Request::Method> parseMethod(llvm::StringRef str) {
  if (equals_insensitive(str, "GET") || equals_insensitive(str, "HEAD"))
    return Request::Method::GET;
  if (equals_insensitive(str, "POST"))
    return Request::Method::POST;
  if (equals_insensitive(str, "PUT"))
    return Request::Method::PUT;
  if (equals_insensitive(str, "DELETE"))
    return Request::Method::DELETE;
  return std::nullopt;
}

HTTPRequest::HTTPRequest(llvm::StringRef method_string, std::optional<URI> uri)
    : Request(parseMethod(method_string), uri) {}
