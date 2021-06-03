#ifndef MEMODB_MULTIBASE_H
#define MEMODB_MULTIBASE_H

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <vector>

namespace memodb {

// https://github.com/multiformats/multibase
class Multibase {
public:
  char Prefix;
  const char *Name;

  Multibase(char Prefix, const char *Name) : Prefix(Prefix), Name(Name) {}
  virtual ~Multibase() {}

  // Decode any multibase with a prefix.
  static std::optional<std::vector<std::uint8_t>> decode(llvm::StringRef Str);
  // Decode a specific multibase without a prefix.
  virtual std::optional<std::vector<std::uint8_t>>
  decodeWithoutPrefix(llvm::StringRef Str) const = 0;
  // Encode a specific multibase with a prefix.
  std::string encode(llvm::ArrayRef<std::uint8_t> Bytes) const;
  // Encode a specific multibase without a prefix.
  virtual std::string
  encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> Bytes) const = 0;

  // May return nullptr if no match is found.
  static const Multibase *findByName(llvm::StringRef Name);

  static const Multibase &Base2;
  static const Multibase &Base8;
  static const Multibase &Base16;
  static const Multibase &Base16Upper;
  static const Multibase &Base32Hex;
  static const Multibase &Base32HexUpper;
  static const Multibase &Base32HexPad;
  static const Multibase &Base32HexPadUpper;
  static const Multibase &Base32;
  static const Multibase &Base32Upper;
  static const Multibase &Base32Pad;
  static const Multibase &Base32PadUpper;
  static const Multibase &Base32z;
  static const Multibase &Base64;
  static const Multibase &Base64Pad;
  static const Multibase &Base64Url;
  static const Multibase &Base64UrlPad;
  static const Multibase &Proquint;
};

} // namespace memodb

#endif // MEMODB_MULTIBASE_H
