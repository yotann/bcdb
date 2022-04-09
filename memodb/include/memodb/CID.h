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

/// A hash or data format used with CIDs.
/// https://github.com/multiformats/multicodec
enum class Multicodec {
  /// IPLD: this is a version 1 CID.
  CIDv1 = 0x01,
  /// IPLD: this CID refers to raw binary data.
  Raw = 0x55,
  /// IPLD: this CID refers to structured data encoded with DAG-CBOR.
  DAG_CBOR = 0x71,
  /// IPLD: this CID refers to structured data encoded with CBOR, with IPLD
  /// links but without the normal DAG-CBOR restrictions.
  DAG_CBOR_Unrestricted = 0x0171,
  /// Multihash: the data is included directly in the CID.
  Identity = 0x00,
  /// Multihash: this CID is based on a 256-bit Blake2b hash of the data.
  Blake2b_256 = 0xb220,
};

/// A unique identifier for a Node value. Usually this is based on a hash of
/// the data, with the hash type and data format also specified. For very small
/// values, this can be an identity CID, which contains the data itself rather
/// than a hash of it. Follows the Content Identifier Specification:
/// https://github.com/multiformats/cid
class CID {
private:
  Multicodec ContentType, HashType;

  std::size_t HashSize;

  // Bytes field consists of the whole encoded CID.
  llvm::SmallVector<std::uint8_t, 48> Bytes;

  CID() {}

public:
  /// Check whether this is an identity CID, containing the data directly.
  bool isIdentity() const { return HashType == Multicodec::Identity; }

  /// Get the format of the data referred to by the CID, such as \ref
  /// Multicodec::DAG_CBOR.
  Multicodec getContentType() const { return ContentType; }

  /// Get a reference to the bytes of the hash only. For an identity CID, this
  /// will get a reference to the raw data bytes.
  llvm::ArrayRef<std::uint8_t> getHashBytes() const;

  /// Calculate a CID for some data. If HashType is not provided, the hash will
  /// be \ref Multicodec::Blake2b_256 or \ref Multicodec::Identity depending on
  /// the size of the data.
  static CID calculate(Multicodec ContentType,
                       llvm::ArrayRef<std::uint8_t> Content,
                       std::optional<Multicodec> HashType = {});

  /// Parse a textual CID that has a multibase prefix. Returns std::nullopt if
  /// invalid or unsupported.
  static std::optional<CID> parse(llvm::StringRef String);

  /// Parse a raw binary CID with an optional 0x00 multibase prefix. Returns
  /// std::nullopt if invalid or unsupported.
  static std::optional<CID> fromBytes(llvm::ArrayRef<std::uint8_t> Bytes);

  /// Like fromBytes(), but ignores extra data after the CID. On successful
  /// return, Bytes will point to the rest of the data after the CID. If
  /// std::nullopt is returned, it's unspecified what Bytes points to.
  static std::optional<CID>
  loadFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes);

  /// Returns the whole CID as a byte string (without any multibase prefix).
  llvm::ArrayRef<std::uint8_t> asBytes() const { return Bytes; }

  /// Returns a textual version of the CID, using the specified Multibase.
  std::string asString(const Multibase &Base) const;

  /// Returns a textual version of the CID, using a default or user-specified
  /// Multibase.
  std::string asString() const;

  /// Returns a textual version of the CID, using a default or user-specified
  /// Multibase.
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
