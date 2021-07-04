#include "memodb/Support.h"

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <optional>
#include <string>

using namespace memodb;

std::string memodb::bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes) {
  std::string Result;
  for (std::uint8_t Byte : Bytes) {
    if (Byte < 0x80) {
      Result.push_back(static_cast<char>(Byte));
    } else {
      Result.push_back(static_cast<char>(0xc0 | (Byte >> 6)));
      Result.push_back(static_cast<char>(0x80 | (Byte & 0x3f)));
    }
  }
  return Result;
}

std::string memodb::bytesToUTF8(llvm::StringRef Bytes) {
  return bytesToUTF8(llvm::ArrayRef(
      reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
}

std::string memodb::utf8ToByteString(llvm::StringRef Str) {
  std::string Result;
  while (!Str.empty()) {
    std::uint8_t x = (std::uint8_t)Str[0];
    if (x < 0x80) {
      Result.push_back(static_cast<char>(x));
      Str = Str.drop_front(1);
    } else {
      std::uint8_t y = Str.size() >= 2 ? (std::uint8_t)Str[1] : 0;
      if ((x & 0xfc) != 0xc0 || (y & 0xc0) != 0x80)
        llvm::report_fatal_error("invalid UTF-8 bytes");
      Result.push_back(static_cast<char>((x & 3) << 6 | (y & 0x3f)));
      Str = Str.drop_front(2);
    }
  }
  return Result;
}

std::optional<URI> URI::parse(llvm::StringRef str, bool allow_relative_path) {
  URI uri;
  llvm::StringRef authority_ref, path_ref, query_ref, fragment_ref;

  if (str.contains(':'))
    std::tie(uri.scheme, str) = str.split(':');
  if (str.startswith("//")) {
    size_t i = str.find_first_of("/?#", 2);
    if (i == llvm::StringRef::npos) {
      authority_ref = str.substr(2);
      str = "";
    } else {
      authority_ref = str.substr(2, i - 2);
      str = str.substr(i);
    }
  }
  std::tie(str, fragment_ref) = str.split('#');
  std::tie(path_ref, query_ref) = str.split('?');

  bool percent_decoding_error = false;

  auto percentDecode =
      [&percent_decoding_error](llvm::StringRef str) -> std::string {
    if (!str.contains('%'))
      return str.str();
    std::string result;
    while (!str.empty()) {
      size_t i = str.find('%');
      result.append(str.take_front(i));
      str = str.substr(i);
      if (str.empty())
        break;
      unsigned code;
      if (str.size() >= 3 && !str.substr(1, 2).getAsInteger(16, code)) {
        result.push_back(static_cast<char>(code));
        str = str.drop_front(3);
      } else {
        percent_decoding_error = true;
        break;
      }
    }
    return result;
  };

  uri.authority = percentDecode(authority_ref);
  uri.fragment = percentDecode(fragment_ref);

  if (!path_ref.empty()) {
    if (!allow_relative_path) {
      if (!path_ref.startswith("/"))
        return std::nullopt;
      path_ref = path_ref.drop_front();
    }
    llvm::SmallVector<llvm::StringRef, 8> segments;
    path_ref.split(segments, '/');
    for (const auto &segment : segments) {
      if (segment == "." || segment == "..")
        return std::nullopt;
      uri.path_segments.emplace_back(percentDecode(segment));
    }
  }

  if (!query_ref.empty()) {
    llvm::SmallVector<llvm::StringRef, 8> params;
    query_ref.split(params, '&');
    for (const auto &param : params)
      uri.query_params.emplace_back(percentDecode(param));
  }

  if (percent_decoding_error)
    return std::nullopt;
  return uri;
}

std::optional<std::string> URI::getPathString() const {
  std::string result;
  for (const auto &segment : path_segments) {
    if (llvm::StringRef(segment).contains('/'))
      return std::nullopt;
    result += "/" + segment;
  }
  return result;
}
