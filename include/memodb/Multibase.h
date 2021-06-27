#ifndef MEMODB_MULTIBASE_H
#define MEMODB_MULTIBASE_H

#include <cstdint>
#include <functional>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <vector>

namespace memodb {

// Multibase is a flexible way to represent binary data using text strings.
// Multiple encodings are supported; the selected encoding is indicated by the
// first character of the string. For example, "bkwva" (base32), "f55aa"
// (base16), and "mVao" (base64) are all representations of the same binary
// data (0x55 0xaa).
//
// Full description: https://github.com/multiformats/multibase
class Multibase {
public:
  char prefix;
  const char *name;

  Multibase(char prefix, const char *name) : prefix(prefix), name(name) {}
  virtual ~Multibase() {}

  // Decode any multibase with a prefix.
  static std::optional<std::vector<std::uint8_t>> decode(llvm::StringRef str);

  // Decode a specific multibase without a prefix.
  virtual std::optional<std::vector<std::uint8_t>>
  decodeWithoutPrefix(llvm::StringRef str) const = 0;

  // Encode a specific multibase with a prefix.
  std::string encode(llvm::ArrayRef<std::uint8_t> bytes) const;

  // Encode a specific multibase without a prefix.
  virtual std::string
  encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> bytes) const = 0;

  // May return nullptr if no match is found.
  static const Multibase *findByName(llvm::StringRef name);

  // Call a function for each available multibase.
  static void eachBase(std::function<void(const Multibase &)> func);

  static const Multibase &base2;
  static const Multibase &base8;
  static const Multibase &base16;
  static const Multibase &base16upper;
  static const Multibase &base32hex;
  static const Multibase &base32hexupper;
  static const Multibase &base32hexpad;
  static const Multibase &base32hexpadupper;
  static const Multibase &base32;
  static const Multibase &base32upper;
  static const Multibase &base32pad;
  static const Multibase &base32padupper;
  static const Multibase &base32z;
  static const Multibase &base64;
  static const Multibase &base64pad;
  static const Multibase &base64url;
  static const Multibase &base64urlpad;
  static const Multibase &proquint;
};

} // namespace memodb

#endif // MEMODB_MULTIBASE_H
