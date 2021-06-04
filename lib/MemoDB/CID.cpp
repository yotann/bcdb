#include "memodb/CID.h"

#include <assert.h>
#include <llvm/Support/CommandLine.h>
#include <sodium/crypto_generichash_blake2b.h>

#include "memodb/Multibase.h"

using namespace memodb;

namespace {
llvm::cl::opt<std::string>
    MultibaseOption("cid-base", llvm::cl::Optional,
                    llvm::cl::desc("Multibase encoding used for CIDs"),
                    llvm::cl::init("base32"));
} // end anonymous namespace

static void writeVarInt(llvm::SmallVectorImpl<std::uint8_t> &Bytes,
                        std::uint64_t Value) {
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
  llvm::SmallVector<std::uint8_t, 32> Buffer;
  llvm::ArrayRef<std::uint8_t> Hash;
  if (*HashType == Multicodec::Identity) {
    Hash = Content;
  } else if (*HashType == Multicodec::Blake2b_256) {
    Buffer.resize(32);
    crypto_generichash_blake2b(Buffer.data(), Buffer.size(), Content.data(),
                               Content.size(), nullptr, 0);
    Hash = Buffer;
  } else {
    assert(false && "unsupported multihash");
    return calculate(ContentType, Content);
  }

  CID Result;
  Result.ContentType = ContentType;
  Result.HashType = *HashType;
  Result.HashSize = Hash.size();
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Multicodec::CIDv1));
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Result.ContentType));
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Result.HashType));
  writeVarInt(Result.Bytes, Result.HashSize);
  Result.Bytes.append(Hash.begin(), Hash.end());
  return Result;
}

std::optional<CID> CID::parse(llvm::StringRef Text) {
  auto Bytes = Multibase::decode(Text);
  if (!Bytes)
    return {};
  return fromBytes(*Bytes);
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

static std::optional<Multicodec>
loadMulticodecFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes) {
  auto Raw = loadVarIntFromSequence(Bytes);
  if (!Raw)
    return {};
  Multicodec Result = static_cast<Multicodec>(*Raw);
  if (static_cast<std::uint64_t>(Result) != *Raw)
    return {}; // too large for our Multicodec enum
  return Result;
}

std::optional<CID> CID::loadFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes) {
  // Optional multibase prefix for binary.
  if (Bytes.size() >= 1 && Bytes[0] == 0x00)
    Bytes = Bytes.drop_front();
  llvm::ArrayRef<std::uint8_t> OrigBytes = Bytes;
  auto RawVersion = loadMulticodecFromSequence(Bytes);
  if (!RawVersion || *RawVersion != Multicodec::CIDv1)
    return {};
  auto RawContentType = loadMulticodecFromSequence(Bytes);
  auto RawHashType = loadMulticodecFromSequence(Bytes);
  auto RawHashSize = loadVarIntFromSequence(Bytes);
  if (!RawContentType || !RawHashType || !RawHashSize)
    return {};
  if (Bytes.size() < RawHashSize)
    return {};
  if (*RawContentType != Multicodec::Raw &&
      *RawContentType != Multicodec::DAG_CBOR)
    return {};
  if (*RawHashType == Multicodec::Identity) {
    // arbitrary sizes allowed
  } else if (*RawHashType == Multicodec::Blake2b_256) {
    if (*RawHashSize != 32)
      return {};
  } else {
    return {}; // unknown hash
  }

  Bytes = Bytes.drop_front(*RawHashSize);
  CID Result;
  Result.ContentType = *RawContentType;
  Result.HashType = *RawHashType;
  Result.HashSize = *RawHashSize;
  OrigBytes = OrigBytes.drop_back(Bytes.size());
  Result.Bytes.append(OrigBytes.begin(), OrigBytes.end());
  return Result;
}

std::string CID::asString(const Multibase &Base) const {
  return Base.encode(asBytes());
}

std::string CID::asString() const {
  auto Base = Multibase::findByName(MultibaseOption);
  if (Base)
    return asString(*Base);
  return asString(Multibase::base32);
}

CID::operator std::string() const { return asString(); }

std::ostream &memodb::operator<<(std::ostream &os, const CID &ref) {
  return os << std::string(ref);
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const CID &ref) {
  return os << std::string(ref);
}
