#include "memodb/CID.h"

#include <sodium.h>

#include "memodb/memodb.h"

using namespace memodb;

// IDs follow the CID specification: https://github.com/multiformats/cid

static const char *BASE32_PREFIX = "b";

static const llvm::StringRef BASE32_TABLE("abcdefghijklmnopqrstuvwxyz234567");

static const size_t HASH_SIZE = crypto_generichash_BYTES;

static const std::array<std::uint8_t, 3> CID_PREFIX_INLINE_RAW = {
    0x01, // CIDv1
    0x55, // content type: raw
    0x00, // multihash function: identity
};

static const std::array<std::uint8_t, 3> CID_PREFIX_INLINE_DAG = {
    0x01, // CIDv1
    0x71, // content type: MerkleDAG CBOR
    0x00, // multihash function: identity
};

static const std::array<std::uint8_t, 6> CID_PREFIX_RAW = {
    0x01,                  // CIDv1
    0x55,                  // content type: raw
    0xa0,      0xe4, 0x02, // multihash function: Blake2B-256
    HASH_SIZE,             // multihash size
};

static const std::array<std::uint8_t, 6> CID_PREFIX_DAG = {
    0x01,                  // CIDv1
    0x71,                  // content type: MerkleDAG CBOR
    0xa0,      0xe4, 0x02, // multihash function: Blake2B-256
    HASH_SIZE,             // multihash size
};

static std::vector<std::uint8_t> decodeBase32(llvm::StringRef Str) {
  std::vector<std::uint8_t> Result;
  while (!Str.empty()) {
    uint64_t Value = 0;
    for (size_t i = 0; i < 8; i++) {
      Value <<= 5;
      if (i < Str.size()) {
        size_t Pos = BASE32_TABLE.find(Str[i]);
        if (Pos == llvm::StringRef::npos)
          llvm::report_fatal_error("invalid character in base32");
        Value |= Pos;
      }
    }

    const size_t NumBytesArray[8] = {0, 0, 1, 0, 2, 3, 0, 4};
    size_t NumBytes = Str.size() >= 8 ? 5 : NumBytesArray[Str.size()];
    if (!NumBytes)
      llvm::report_fatal_error("invalid length of base32");
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

CID::CID(llvm::StringRef Text) {
  if (Text.startswith(BASE32_PREFIX)) {
    *this = fromCID(decodeBase32(Text.drop_front()));
  } else {
    llvm::report_fatal_error(llvm::Twine("unsupported multibase ") + Text);
  }
}

CID CID::fromCID(llvm::ArrayRef<std::uint8_t> Bytes) {
  CID Result = loadCIDFromSequence(Bytes);
  if (!Bytes.empty())
    llvm::report_fatal_error("Extra bytes in CID");
  return Result;
}

CID CID::loadCIDFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes) {
  auto startsWith = [&](const auto &Prefix) {
    return Bytes.take_front(Prefix.size()).equals(Prefix);
  };
  if (startsWith(CID_PREFIX_RAW)) {
    Bytes = Bytes.drop_front(CID_PREFIX_RAW.size());
    CID Result = fromBlake2BRaw(Bytes.take_front(HASH_SIZE));
    Bytes = Bytes.drop_front(HASH_SIZE);
    return Result;
  } else if (startsWith(CID_PREFIX_DAG)) {
    Bytes = Bytes.drop_front(CID_PREFIX_RAW.size());
    CID Result = fromBlake2BMerkleDAG(Bytes.take_front(HASH_SIZE));
    Bytes = Bytes.drop_front(HASH_SIZE);
    return Result;
  } else if (startsWith(CID_PREFIX_INLINE_RAW)) {
    Bytes = Bytes.drop_front(CID_PREFIX_INLINE_RAW.size());
    if (Bytes.empty() || Bytes[0] >= 0x80 || Bytes[0] > Bytes.size() - 1)
      llvm::report_fatal_error("invalid inline CID");
    size_t Size = Bytes[0];
    CID Result;
    Result.id_ = Bytes.slice(1, Size);
    Result.type_ = INLINE_RAW;
    Bytes = Bytes.drop_front(1 + Size);
    return Result;
  } else if (startsWith(CID_PREFIX_INLINE_DAG)) {
    Bytes = Bytes.drop_front(CID_PREFIX_INLINE_DAG.size());
    if (Bytes.empty() || Bytes[0] >= 0x80 || Bytes[0] > Bytes.size() - 1)
      llvm::report_fatal_error("invalid inline CID");
    size_t Size = Bytes[0];
    CID Result;
    Result.id_ = Bytes.slice(1, Size);
    Result.type_ = INLINE_DAG;
    Bytes = Bytes.drop_front(1 + Size);
    return Result;
  } else {
    llvm::report_fatal_error(llvm::Twine("invalid CID"));
  }
}

CID CID::fromBlake2BRaw(llvm::ArrayRef<std::uint8_t> Bytes) {
  if (Bytes.size() != HASH_SIZE)
    llvm::report_fatal_error("incorrect Blake2B hash size");
  CID Result;
  Result.id_ = Bytes;
  Result.type_ = BLAKE2B_RAW;
  return Result;
}

CID CID::fromBlake2BMerkleDAG(llvm::ArrayRef<std::uint8_t> Bytes) {
  if (Bytes.size() != HASH_SIZE)
    llvm::report_fatal_error("incorrect Blake2B hash size");
  CID Result;
  Result.id_ = Bytes;
  Result.type_ = BLAKE2B_MERKLEDAG;
  return Result;
}

memodb_value CID::asInline() const {
  if (type_ == INLINE_RAW)
    return memodb_value(id_);
  else if (type_ == INLINE_DAG)
    return memodb_value::load_cbor(id_);
  else
    llvm::report_fatal_error("incorrect type of ID");
}

std::vector<std::uint8_t> CID::asCID() const {
  std::vector<std::uint8_t> Result;
  if (type_ == BLAKE2B_RAW)
    Result.assign(CID_PREFIX_RAW.begin(), CID_PREFIX_RAW.end());
  else if (type_ == BLAKE2B_MERKLEDAG)
    Result.assign(CID_PREFIX_DAG.begin(), CID_PREFIX_DAG.end());
  else if (type_ == INLINE_RAW) {
    Result.assign(CID_PREFIX_INLINE_RAW.begin(), CID_PREFIX_INLINE_RAW.end());
    Result.push_back(id_.size());
  } else if (type_ == INLINE_DAG) {
    Result.assign(CID_PREFIX_INLINE_DAG.begin(), CID_PREFIX_INLINE_DAG.end());
    Result.push_back(id_.size());
  } else
    llvm::report_fatal_error("ID not a CID");
  Result.insert(Result.end(), id_.begin(), id_.end());
  return Result;
}

CID::operator std::string() const {
  if (type_ == EMPTY)
    return "";
  else {
    auto Bytes = asCID();
    return BASE32_PREFIX + encodeBase32(Bytes);
  }
}

CID CID::calculateFromContent(llvm::ArrayRef<std::uint8_t> Content, bool raw,
                              bool noInline) {
  if (!noInline && CID_PREFIX_INLINE_DAG.size() + 1 + Content.size() <=
                       CID_PREFIX_DAG.size() + HASH_SIZE) {
    CID Ref;
    Ref.id_ = Content;
    Ref.type_ = raw ? CID::INLINE_RAW : CID::INLINE_DAG;
    return Ref;
  } else {
    CID Ref;
    Ref.id_.resize(HASH_SIZE);
    Ref.type_ = raw ? CID::BLAKE2B_RAW : CID::BLAKE2B_MERKLEDAG;
    crypto_generichash(Ref.id_.data(), Ref.id_.size(), Content.data(),
                       Content.size(), nullptr, 0);
    return Ref;
  }
}

std::ostream &memodb::operator<<(std::ostream &os, const CID &ref) {
  return os << std::string(ref);
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const CID &ref) {
  return os << std::string(ref);
}
