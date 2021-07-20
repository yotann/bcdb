#ifndef MEMODB_URI_H
#define MEMODB_URI_H

#include <llvm/ADT/StringRef.h>
#include <optional>
#include <string>
#include <vector>

namespace memodb {

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

#endif // MEMODB_URI_H
