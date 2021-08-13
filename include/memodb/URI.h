#ifndef MEMODB_URI_H
#define MEMODB_URI_H

#include <llvm/ADT/StringRef.h>
#include <optional>
#include <string>
#include <vector>

namespace memodb {

/// A generic URI parsed into its components. This class is only designed to
/// handle `file:`, `http(s):`, and other schemes that use equivalent syntax.
/// The userinfo field is not supported. Empty hosts and fragments are not
/// distinguished from missing hosts and fragments.
struct URI {
public:
  static std::optional<URI> parse(llvm::StringRef str,
                                  bool allow_dot_segments = false);

  /// Returns `path_segments[first_index:]` joined by "/". If @p first_index is
  /// 0 and rootless is false, there will be an extra "/" in front.
  ///
  /// \warning this function can return paths with dot segments, even if
  /// allow_dot_segments was false!
  std::string getPathString(unsigned first_index = 0) const;

  /// Encode the URI using normal form.
  std::string encode() const;

  bool operator==(const URI &other) const;
  bool operator!=(const URI &other) const;

  // If input is "x:/y/foo%2Fbar", path_segments will be ["y", "foo/bar"].
  std::string scheme, host, fragment;
  unsigned port = 0;
  bool rootless = false;
  std::vector<std::string> path_segments;
  std::vector<std::string> query_params;
  bool escape_slashes_in_segments = true;
};

} // end namespace memodb

#endif // MEMODB_URI_H
