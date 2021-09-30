#include "memodb/Request.h"

#include <algorithm>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/xxhash.h>
#include <optional>
#include <string>

#include "memodb/CID.h"
#include "memodb/Multibase.h"
#include "memodb/Node.h"
#include "memodb/URI.h"

using namespace memodb;
using llvm::SmallVector;
using llvm::StringRef;

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
  } else {
    sendContentNode(node, std::nullopt, cache_control);
  }
}
