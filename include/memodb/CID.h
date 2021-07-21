#ifndef MEMODB_CID_H
#define MEMODB_CID_H

#include <cstdint>
#include <iosfwd>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <vector>

namespace memodb {

class Multibase;

// A hash or data format used with CIDs.
// https://github.com/multiformats/multicodec
enum class Multicodec {
  Identity = 0x00,
  CIDv1 = 0x01,
  Raw = 0x55,
  DAG_CBOR = 0x71,
  Blake2b_256 = 0xb220,
};

// A unique identifier for some data. Usually this is based on a hash of the
// data, with the hash type and data format also specified. Follows the Content
// Identifier Specification:
// https://github.com/multiformats/cid
class CID {
private:
  Multicodec ContentType, HashType;

  std::size_t HashSize;

  // Bytes field consists of the whole encoded CID.
  llvm::SmallVector<std::uint8_t, 48> Bytes;

  CID() {}

public:
  // An identity CID actually contains the data itself, not a hash of it.
  bool isIdentity() const { return HashType == Multicodec::Identity; }

  // Get the format of the data referred to by the CID.
  Multicodec getContentType() const { return ContentType; }

  // Get a reference to the bytes of the hash only.
  llvm::ArrayRef<std::uint8_t> getHashBytes() const;

  // Calculate a CID for some data. If HashType is not provided, the hash will
  // be Blake2b_256 or Identity depending on the size of the data.
  static CID calculate(Multicodec ContentType,
                       llvm::ArrayRef<std::uint8_t> Content,
                       std::optional<Multicodec> HashType = {});

  // Parse a textual CID that has a multibase prefix. Returns std::nullopt if
  // invalid or unsupported.
  static std::optional<CID> parse(llvm::StringRef String);

  // Parse a raw binary CID with an optional 0x00 multibase prefix. Returns
  // std::nullopt if invalid or unsupported.
  static std::optional<CID> fromBytes(llvm::ArrayRef<std::uint8_t> Bytes);

  // Like fromBytes(), but ignores extra data after the CID. On successful
  // return, Bytes will point to the rest of the data after the CID. If
  // std::nullopt is returned, it's unspecified what Bytes points to.
  static std::optional<CID>
  loadFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes);

  // Return the whole CID as a byte string (without any multibase prefix).
  llvm::ArrayRef<std::uint8_t> asBytes() const { return Bytes; }

  // Returns a textual version of the CID, using the specified Multibase.
  std::string asString(const Multibase &Base) const;

  // Returns a textual version of the CID, using a default or user-specified
  // Multibase.
  std::string asString() const;
  operator std::string() const;

  bool operator<(const CID &Other) const { return Bytes < Other.Bytes; }
  bool operator>(const CID &Other) const { return Other.Bytes < Bytes; }
  bool operator<=(const CID &Other) const { return !(Other.Bytes < Bytes); }
  bool operator>=(const CID &Other) const { return !(Bytes < Other.Bytes); }
  bool operator==(const CID &Other) const { return Bytes == Other.Bytes; }
  bool operator!=(const CID &Other) const { return Bytes != Other.Bytes; }
};

// Writes a textual version of the CID, using a default or user-specified
// Multibase.
std::ostream &operator<<(std::ostream &os, const CID &ref);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const CID &ref);

} // end namespace memodb

#endif // MEMODB_CID_H
