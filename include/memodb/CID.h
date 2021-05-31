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

// https://github.com/multiformats/multicodec
enum class Multicodec {
  Identity = 0x00,
  CIDv1 = 0x01,
  Raw = 0x55,
  DAG_CBOR = 0x71,
  Blake2b_256 = 0xb220,
};

// A content hash that follows the Content Identifier Specification:
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

  Multicodec getContentType() const { return ContentType; }

  llvm::ArrayRef<std::uint8_t> getHashBytes() const {
    return llvm::ArrayRef<std::uint8_t>(Bytes).take_back(HashSize);
  }

  static CID calculate(Multicodec ContentType,
                       llvm::ArrayRef<std::uint8_t> Content,
                       std::optional<Multicodec> HashType = {});

  static std::optional<CID> parse(llvm::StringRef String);

  // Parse a raw binary CID with an optional 0x00 multibase prefix. Returns
  // std::nullopt if invalid or unsupported.
  static std::optional<CID> fromBytes(llvm::ArrayRef<std::uint8_t> Bytes);

  // Like fromBytes(), but ignores extra data after the CID. On successful
  // return, Bytes will point to the data after the CID. If std::nullopt is
  // returned, it's unspecified what Bytes points to.
  static std::optional<CID>
  loadFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes);

  llvm::ArrayRef<std::uint8_t> asBytes() const { return Bytes; }

  std::string asString(const Multibase &Base) const;

  // Use user-specified multibase.
  std::string asString() const;
  operator std::string() const;

  bool operator<(const CID &Other) const { return Bytes < Other.Bytes; }
  bool operator>(const CID &Other) const { return Other.Bytes < Bytes; }
  bool operator<=(const CID &Other) const { return !(Other.Bytes < Bytes); }
  bool operator>=(const CID &Other) const { return !(Bytes < Other.Bytes); }
  bool operator==(const CID &Other) const { return Bytes == Other.Bytes; }
  bool operator!=(const CID &Other) const { return Bytes != Other.Bytes; }
};

std::ostream &operator<<(std::ostream &os, const CID &ref);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const CID &ref);

} // end namespace memodb

#endif // MEMODB_CID_H
