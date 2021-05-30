#include "memodb/CID.h"

#include <assert.h>
#include <cmath>
#include <sodium.h>

using namespace memodb;

namespace {
struct MultibaseCodec {
  static const std::int8_t INVALID_VALUE = -1;
  static const std::int8_t PAD_VALUE = -2;

  MultibaseCodec(Multibase ID, char Prefix, llvm::StringRef Chars,
                 std::optional<char> Pad)
      : ID(ID), Prefix(Prefix), Chars(Chars), Pad(Pad) {
    for (int i = 0; i < 256; i++)
      Values[i] = INVALID_VALUE;
    for (size_t i = 0; i < Chars.size(); i++)
      Values[(std::uint8_t)Chars[i]] = i;
    if (Pad)
      Values[(std::uint8_t)*Pad] = PAD_VALUE;

    BitsPerChar = std::ilogb(Chars.size());
    assert((1ul << BitsPerChar) == Chars.size());
  }

  std::optional<std::vector<std::uint8_t>> decode(llvm::StringRef Str) const;
  std::string encode(llvm::ArrayRef<std::uint8_t> Bytes) const;

  Multibase ID;
  char Prefix;
  llvm::StringRef Chars;
  std::optional<char> Pad;
  std::int8_t Values[256];
  unsigned BitsPerChar;
};
} // end anonymous namespace

std::optional<std::vector<std::uint8_t>>
MultibaseCodec::decode(llvm::StringRef Str) const {
  std::vector<std::uint8_t> Result;
  bool PadSeen = false;
  while (!Str.empty()) {
    std::uint64_t Value = 0;
    size_t NumBits = 0, ValidBits = 0;
    do {
      Value <<= BitsPerChar;
      NumBits += BitsPerChar;
      if (Str.empty() && Pad)
        return {}; // missing padding
      if (Str.empty())
        continue;
      std::int8_t CharValue = Values[(std::uint8_t)Str[0]];
      Str = Str.drop_front();
      if (CharValue == INVALID_VALUE)
        return {}; // invalid char
      if (CharValue == PAD_VALUE && !ValidBits)
        return {}; // too much padding
      if (CharValue == PAD_VALUE) {
        PadSeen = true;
      } else {
        if (PadSeen)
          return {}; // chars after padding
        ValidBits += BitsPerChar;
        Value |= CharValue;
      }
    } while (NumBits % 8);
    while (ValidBits >= 8) {
      ValidBits -= 8;
      NumBits -= 8;
      Result.push_back((Value >> NumBits) & 0xff);
    }
    if (ValidBits >= BitsPerChar)
      return {}; // unused char
  }
  return Result;
}

std::string MultibaseCodec::encode(llvm::ArrayRef<std::uint8_t> Bytes) const {
  std::string Result;
  while (!Bytes.empty()) {
    uint64_t Value = 0;
    size_t NumBits = 0, ValidBits = 0;
    do {
      Value <<= 8;
      NumBits += 8;
      if (!Bytes.empty()) {
        Value |= Bytes[0];
        Bytes = Bytes.drop_front();
        ValidBits += 8;
      }
    } while (NumBits % BitsPerChar);
    while (ValidBits) {
      ValidBits -= ValidBits > BitsPerChar ? BitsPerChar : ValidBits;
      NumBits -= BitsPerChar;
      Result.push_back(Chars[(Value >> NumBits) & ((1 << BitsPerChar) - 1)]);
    }
    while (Pad && NumBits) {
      NumBits -= BitsPerChar;
      Result.push_back(*Pad);
    }
  }
  return Result;
}

static const MultibaseCodec MULTIBASE_CODECS[] = {
    MultibaseCodec(Multibase::Base2, '0', "01", std::nullopt),
    MultibaseCodec(Multibase::Base16, 'f', "0123456789abcdef", std::nullopt),
    MultibaseCodec(Multibase::Base16Upper, 'F', "0123456789ABCDEF",
                   std::nullopt),
    MultibaseCodec(Multibase::Base32Hex, 'v',
                   "0123456789abcdefghijklmnopqrstuv", std::nullopt),
    MultibaseCodec(Multibase::Base32HexUpper, 'V',
                   "0123456789ABCDEFGHIJKLMNOPQRSTUV", std::nullopt),
    MultibaseCodec(Multibase::Base32HexPad, 't',
                   "0123456789abcdefghijklmnopqrstuv", '='),
    MultibaseCodec(Multibase::Base32HexPadUpper, 'T',
                   "0123456789ABCDEFGHIJKLMNOPQRSTUV", '='),
    MultibaseCodec(Multibase::Base32, 'b', "abcdefghijklmnopqrstuvwxyz234567",
                   std::nullopt),
    MultibaseCodec(Multibase::Base32Upper, 'B',
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", std::nullopt),
    MultibaseCodec(Multibase::Base32Pad, 'c',
                   "abcdefghijklmnopqrstuvwxyz234567", '='),
    MultibaseCodec(Multibase::Base32PadUpper, 'C',
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", '='),
    MultibaseCodec(
        Multibase::Base64, 'm',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
        std::nullopt),
    MultibaseCodec(
        Multibase::Base64Pad, 'M',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
        '='),
    MultibaseCodec(
        Multibase::Base64Url, 'u',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
        std::nullopt),
    MultibaseCodec(
        Multibase::Base64UrlPad, 'U',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
        '='),
};

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

  CID Result;
  Result.ContentType = ContentType;
  Result.HashType = *HashType;
  Result.HashSize = Hash.size();
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Multicodec::CIDv1));
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Result.ContentType));
  writeVarInt(Result.Bytes, static_cast<std::uint64_t>(Result.HashType));
  writeVarInt(Result.Bytes, Result.HashSize);
  Result.Bytes.insert(Result.Bytes.end(), Hash.begin(), Hash.end());
  return Result;
}

std::optional<CID> CID::parse(llvm::StringRef Text) {
  if (Text.empty())
    return {};
  for (const MultibaseCodec &Codec : MULTIBASE_CODECS) {
    if (Text[0] == Codec.Prefix) {
      auto Bytes = Codec.decode(Text.drop_front());
      if (!Bytes)
        return {};
      return fromBytes(*Bytes);
    }
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
  OrigBytes = OrigBytes.drop_back(Bytes.size());
  Result.Bytes.insert(Result.Bytes.end(), OrigBytes.begin(), OrigBytes.end());
  return Result;
}

std::string CID::asString(Multibase Base) const {
  for (const auto &Codec : MULTIBASE_CODECS)
    if (Codec.ID == Base)
      return Codec.Prefix + Codec.encode(asBytes());
  assert(false && "unsupported multibase");
  return asString(Multibase::Base32);
}

CID::operator std::string() const { return asString(); }

std::ostream &memodb::operator<<(std::ostream &os, const CID &ref) {
  return os << std::string(ref);
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const CID &ref) {
  return os << std::string(ref);
}
