#ifndef MEMODB_SUPPORT_H
#define MEMODB_SUPPORT_H

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <optional>
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

// A generic URI parsed into its components. This class is only designed to
// handle "file:", "http(s):", and other schemes that use equivalent syntax.
// The userinfo field is not supported. Empty hosts and fragments are not
// distinguished from missing hosts and fragments.
struct URI {
public:
  static std::optional<URI> parse(llvm::StringRef str,
                                  bool allow_dot_segments = false);

  // Returns path_segments joined by "/", with an extra "/" in front.
  // Return std::nullopt if any path segment contains a "/".
  std::optional<std::string> getPathString() const;

  // Encode the URI using normal form.
  std::string encode() const;

  // If input is "x:/y/foo%2Fbar", path_segments will be ["y", "foo/bar"].
  std::string scheme, host, fragment;
  unsigned port = 0;
  bool rootless = false;
  std::vector<std::string> path_segments;
  std::vector<std::string> query_params;
};

} // end namespace memodb

#endif // MEMODB_SUPPORT_H
