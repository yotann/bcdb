#ifndef MEMODB_SUPPORT_H
#define MEMODB_SUPPORT_H

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <type_traits>
#include <vector>

namespace memodb {

// Allows static_assert to be used to mark a template instance as
// unimplemented.
template <class T> struct Unimplemented : std::false_type {};

// Helps create a callable type for use with std::visit.
// https://en.cppreference.com/w/cpp/utility/variant/visit
template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

// LLVM symbol names are usually ASCII, but can contain arbitrary bytes. We
// interpret the bytes as ISO-8859-1 and convert them to UTF-8 for use in map
// keys.
std::string bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes);
std::string bytesToUTF8(llvm::StringRef Bytes);
std::string utf8ToByteString(llvm::StringRef Str);

// Parse a generic URI into its components.
struct ParsedURI {
public:
  ParsedURI(llvm::StringRef URI);

  // If input is "x:/foo%2Fbar", Path will be "/foo/bar" and PathSegments will
  // be ["", "foo%2Fbar"].
  std::string Scheme, Authority, Path, Query, Fragment;
  std::vector<std::string> PathSegments;
};

} // end namespace memodb

#endif // MEMODB_SUPPORT_H
