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

void HTTPRequest::sendError(Status status, std::optional<llvm::StringRef> type,
                            llvm::StringRef title,
                            const std::optional<llvm::Twine> &detail) {
  sendStatus(static_cast<std::uint16_t>(status));
  return sendErrorAfterStatus(status, type, title, detail);
}

void HTTPRequest::sendMethodNotAllowed(llvm::StringRef allow) {
  sendStatus(405);
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

void HTTPRequest::sendContentNode(const Node &node,
                                  const std::optional<CID> &cid_if_known,
                                  CacheControl cache_control) {
  unsigned octet_stream_quality = getAcceptQuality(ContentType::OctetStream);
  unsigned json_quality = getAcceptQuality(ContentType::JSON);
  unsigned cbor_quality = getAcceptQuality(ContentType::CBOR);
  unsigned html_quality = getAcceptQuality(ContentType::HTML);

  // TODO: add Cache-Control, ETag, and Server headers.

  if (node.kind() == Kind::Bytes && octet_stream_quality >= json_quality &&
      octet_stream_quality >= cbor_quality &&
      octet_stream_quality >= html_quality) {
    sendStatus(200);
    sendHeader("Content-Type", "application/octet-stream");
    sendBody(node.as<llvm::StringRef>(byte_string_arg));
    return;
  }

  if (html_quality > cbor_quality && html_quality > json_quality) {
    std::string cid_string = "MemoDB Node";
    if (cid_if_known)
      cid_string = cid_if_known->asString(Multibase::base64url);
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
  } else if (cbor_quality != 0 && cbor_quality >= json_quality) {
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
