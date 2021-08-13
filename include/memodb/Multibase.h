#ifndef MEMODB_MULTIBASE_H
#define MEMODB_MULTIBASE_H

#include <cstdint>
#include <functional>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <vector>

namespace memodb {

/**
 * A flexible way to represent binary data using text strings.
 * Multiple encodings are supported; the selected encoding is indicated by the
 * first character of the string. For example, `bkwva` (base32), `f55aa`
 * (base16), and `mVao` (base64) are all representations of the same binary
 * data (`0x55 0xaa`).
 *
 * Full description: https://github.com/multiformats/multibase
 */
class Multibase {
public:
  /// The prefix character, such as 'b' for base32.
  char prefix;

  /// The official name of this multibase, such as "base32" or "base64urlpad".
  const char *name;

  Multibase(char prefix, const char *name) : prefix(prefix), name(name) {}
  virtual ~Multibase() {}

  /**
   * Decode any multibase, using the prefix character to choose the multibase.
   *
   * For example, `decode("bkwva")` will return `{0x55, 0xaa}`.
   *
   * \returns The decoded binary data, or std::nullopt if the string is invalid.
   */
  static std::optional<std::vector<std::uint8_t>> decode(llvm::StringRef str);

  /**
   * Decode a specific multibase, from a string without a prefix.
   *
   * For example, `Multibase::base32.decode("kwva")` will return `{0x55, 0xaa}`.
   *
   * \returns The decoded binary data, or std::nullopt if the string is invalid.
   */
  virtual std::optional<std::vector<std::uint8_t>>
  decodeWithoutPrefix(llvm::StringRef str) const = 0;

  /**
   * Encode a specific multibase, adding a prefix character.
   *
   * For example, `Multibase::base32.encode({0x55, 0xaa})` will return `"bkwva"`.
   */
  std::string encode(llvm::ArrayRef<std::uint8_t> bytes) const;

  /**
   * Encode a specific multibase without adding a prefix character. For
   * example, this will return `"kwva"`:
   * \code
   *   Multibase::base32.encodeWithoutPrefix({0x55, 0xaa});
   * \endcode
   */
  virtual std::string
  encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> bytes) const = 0;

  /**
   * Find a Multibase which has the specified name, such as "base32".
   *
   * May return nullptr if no match is found.
   */
  static const Multibase *findByName(llvm::StringRef name);

  /**
   * Call a function for each available multibase.
   */
  static void eachBase(std::function<void(const Multibase &)> func);

  /// Hexadecimal, lowercase.
  static const Multibase &base16;
  /// Hexadecimal, uppercase.
  static const Multibase &base16upper;
  /// Base32, lowercase, without padding.
  static const Multibase &base32;
  /// Base32, uppercase, without padding.
  static const Multibase &base32upper;
  /// Base64 without padding.
  static const Multibase &base64;
  /// Base64 with padding.
  static const Multibase &base64pad;
  /// Base64url without padding.
  static const Multibase &base64url;
  /// Base64url with padding.
  static const Multibase &base64urlpad;
};

} // namespace memodb

#endif // MEMODB_MULTIBASE_H
