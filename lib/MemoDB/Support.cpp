#include "memodb/Support.h"

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
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

ParsedURI::ParsedURI(llvm::StringRef URI) {
  llvm::StringRef AuthorityRef, PathRef, QueryRef, FragmentRef;

  if (URI.contains(':'))
    std::tie(Scheme, URI) = URI.split(':');
  if (URI.startswith("//")) {
    size_t i = URI.find_first_of("/?#", 2);
    if (i == llvm::StringRef::npos) {
      AuthorityRef = URI.substr(2);
      URI = "";
    } else {
      AuthorityRef = URI.substr(2, i - 2);
      URI = URI.substr(i);
    }
  }
  std::tie(URI, FragmentRef) = URI.split('#');
  std::tie(PathRef, QueryRef) = URI.split('?');

  auto percentDecode = [](llvm::StringRef Str) -> std::string {
    if (!Str.contains('%'))
      return Str.str();
    std::string Result;
    while (!Str.empty()) {
      size_t i = Str.find('%');
      Result.append(Str.take_front(i));
      Str = Str.substr(i);
      if (Str.empty())
        break;
      unsigned Code;
      if (Str.size() >= 3 && !Str.substr(1, 2).getAsInteger(16, Code)) {
        Result.push_back(static_cast<char>(Code));
        Str = Str.substr(3);
      } else {
        llvm::report_fatal_error("invalid percent encoding in URI");
      }
    }
    return Result;
  };

  Authority = percentDecode(AuthorityRef);
  Path = percentDecode(PathRef);
  Query = percentDecode(QueryRef);
  Fragment = percentDecode(FragmentRef);

  llvm::SmallVector<llvm::StringRef, 8> Segments;
  PathRef.split(Segments, '/');
  for (const auto &Segment : Segments)
    PathSegments.emplace_back(percentDecode(Segment));
}
