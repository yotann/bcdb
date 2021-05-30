#include "memodb/CID.h"

#include <assert.h>
#include <sodium.h>

using namespace memodb;

// IDs follow the CID specification: https://github.com/multiformats/cid

static const char *BASE32_PREFIX = "b";

static const llvm::StringRef BASE32_TABLE("abcdefghijklmnopqrstuvwxyz234567");

static std::optional<std::vector<std::uint8_t>>
decodeBase32(llvm::StringRef Str) {
  std::vector<std::uint8_t> Result;
  while (!Str.empty()) {
    uint64_t Value = 0;
    for (size_t i = 0; i < 8; i++) {
      Value <<= 5;
      if (i < Str.size()) {
        size_t Pos = BASE32_TABLE.find(Str[i]);
        if (Pos == llvm::StringRef::npos)
          return {}; // invalid character
        Value |= Pos;
      }
    }

    const size_t NumBytesArray[8] = {0, 0, 1, 0, 2, 3, 0, 4};
    size_t NumBytes = Str.size() >= 8 ? 5 : NumBytesArray[Str.size()];
    if (!NumBytes)
      return {}; // invalid length
    for (size_t i = 0; i < NumBytes; i++)
      Result.push_back((Value >> (32 - 8 * i)) & 0xff);
    Str = Str.drop_front(Str.size() >= 8 ? 8 : Str.size());
  }
  return Result;
}

static std::string encodeBase32(llvm::ArrayRef<std::uint8_t> Bytes) {
  std::string Result;
  while (!Bytes.empty()) {
    uint64_t Value = 0;
    for (size_t i = 0; i < 5; i++)
      Value = (Value << 8) | (i < Bytes.size() ? Bytes[i] : 0);
    const size_t NumCharsArray[5] = {0, 2, 4, 5, 7};
    size_t NumChars = Bytes.size() >= 5 ? 8 : NumCharsArray[Bytes.size()];
    for (size_t i = 0; i < NumChars; i++)
      Result.push_back(BASE32_TABLE[(Value >> (35 - 5 * i)) & 0x1f]);
    Bytes = Bytes.drop_front(Bytes.size() >= 5 ? 5 : Bytes.size());
  }
  return Result;
}

static void writeVarInt(std::vector<std::uint8_t> &Bytes, std::uint64_t Value) {
  for (; Value >= 0x80; Value >>= 7)
    Bytes.push_back((Value & 0x7f) | 0x80);
  Bytes.push_back(Value);
}

CID CID::calculate(Multicodec ContentType, llvm::ArrayRef<std::uint8_t> Content,
                   std::optional<Multicodec> HashType) {
  if (!HashType) {
    size_t IdentitySize = Content.size();
    size_t Blake2BSize = 32 + 2; // VarInt encoded HashType is 2 bytes longer
    HashType = IdentitySize <= Blake2BSize ? Multicodec::Identity
                                           : Multicodec::Blake2b_256;
  }
  CID Result;
  Result.ContentType = ContentType;
  Result.HashType = *HashType;
  Result.Bytes = {0x00};
  std::vector<std::uint8_t> Buffer;
  llvm::ArrayRef<std::uint8_t> Hash;
  if (*HashType == Multicodec::Identity) {
    Hash = Content;
  } else if (*HashType == Multicodec::Blake2b_256) {
    Buffer.resize(32);
    crypto_generichash(Buffer.data(), Buffer.size(), Content.data(),
                       Content.size(), nullptr, 0);
    Hash = Buffer;
  } else {
    assert(false && "unsupported multihash");
    return calculate(ContentType, Content);
  }
  Result.HashSize = Hash.size();
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Multicodec::CIDv1));
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Result.ContentType));
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Result.HashType));
  writeVarInt(Result.Bytes, Result.HashSize);
  Result.Bytes.insert(Result.Bytes.end(), Hash.begin(), Hash.end());
  return Result;
}

std::optional<CID> CID::parse(llvm::StringRef Text) {
  if (Text.startswith(BASE32_PREFIX)) {
    auto Bytes = decodeBase32(Text.drop_front());
    if (!Bytes)
      return {};
    return fromBytes(*Bytes);
  }
  return {};
}

std::optional<CID> CID::fromBytes(llvm::ArrayRef<std::uint8_t> Bytes) {
  auto Result = loadFromSequence(Bytes);
  if (!Bytes.empty())
    return {}; // extra bytes
  return Result;
}

static std::optional<std::uint64_t>
loadVarIntFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes) {
  std::uint64_t Result = 0;
  for (unsigned Shift = 0; true; Shift += 7) {
    if (Shift >= 64 - 7)
      return {}; // too large
    if (Bytes.empty())
      return {}; // incomplete VarInt
    std::uint8_t Byte = Bytes[0];
    Bytes = Bytes.drop_front();
    Result |= (std::uint64_t)(Byte & 0x7f) << Shift;
    if (!(Byte & 0x80)) {
      if (!Byte && Shift)
        return {}; // extra trailing zeros
      break;
    }
  }
  return Result;
}

std::optional<CID> CID::loadFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes) {
  // Optional multibase prefix for binary.
  if (Bytes.size() >= 1 && Bytes[0] == 0x00)
    Bytes = Bytes.drop_front();
  llvm::ArrayRef<std::uint8_t> OrigBytes = Bytes;
  auto RawVersion = loadVarIntFromSequence(Bytes);
  auto RawContentType = loadVarIntFromSequence(Bytes);
  auto RawHashType = loadVarIntFromSequence(Bytes);
  auto RawHashSize = loadVarIntFromSequence(Bytes);
  if (!RawVersion || !RawContentType || !RawHashType || !RawHashSize)
    return {};
  if (Bytes.size() < RawHashSize)
    return {};
  if (*RawVersion != static_cast<std::uint64_t>(Multicodec::CIDv1))
    return {};
  if (*RawContentType != static_cast<std::uint64_t>(Multicodec::Raw) &&
      *RawContentType != static_cast<std::uint64_t>(Multicodec::DAG_CBOR))
    return {};
  if (*RawHashType != static_cast<std::uint64_t>(Multicodec::Identity) &&
      *RawHashType != static_cast<std::uint64_t>(Multicodec::Blake2b_256))
    return {};
  if (*RawHashType == static_cast<std::uint64_t>(Multicodec::Blake2b_256) &&
      *RawHashSize != 32)
    return {};

  Bytes = Bytes.drop_front(*RawHashSize);
  CID Result;
  Result.ContentType = static_cast<Multicodec>(*RawContentType);
  Result.HashType = static_cast<Multicodec>(*RawHashType);
  Result.HashSize = *RawHashSize;
  Result.Bytes = {0x00};
  OrigBytes = OrigBytes.drop_back(Bytes.size());
  Result.Bytes.insert(Result.Bytes.end(), OrigBytes.begin(), OrigBytes.end());
  return Result;
}

llvm::ArrayRef<std::uint8_t> CID::asBytes(bool WithMultibasePrefix) const {
  if (WithMultibasePrefix)
    return Bytes;
  else
    return llvm::ArrayRef(Bytes).drop_front(1);
}

std::string CID::asString(Multibase Base) const {
  if (Base == Multibase::Base32)
    return static_cast<char>(Base) + encodeBase32(asBytes());
  else {
    assert(false && "unsupported multibase");
    return asString(Multibase::Base32);
  }
}

CID::operator std::string() const { return asString(); }

std::ostream &memodb::operator<<(std::ostream &os, const CID &ref) {
  return os << std::string(ref);
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const CID &ref) {
  return os << std::string(ref);
}
