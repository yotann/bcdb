#include "memodb/CID.h"

#include <assert.h>
#include <cmath>
#include <llvm/Support/CommandLine.h>
#include <sodium/crypto_generichash_blake2b.h>

using namespace memodb;

namespace {
llvm::cl::opt<std::string>
    MultibaseOption("multibase", llvm::cl::Optional,
                    llvm::cl::desc("Multibase to print CIDs in"),
                    llvm::cl::init("base32"));
} // end anonymous namespace

namespace {
struct MultibaseCodec {
  static const std::int8_t INVALID_VALUE = -1;
  static const std::int8_t PAD_VALUE = -2;

  MultibaseCodec(llvm::StringRef Name, Multibase ID, char Prefix,
                 llvm::StringRef Chars, std::optional<char> Pad)
      : Name(Name), ID(ID), Prefix(Prefix), Chars(Chars), Pad(Pad) {
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

  llvm::StringRef Name;
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
  // XXX: strings are accepted even if their pad bits are nonzero (e.g.,
  // "cafkqaab=" is equivalent to "cafkqaaa=").
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

std::optional<std::vector<std::uint8_t>> proquintDecode(llvm::StringRef Str) {
  static const llvm::StringRef CON = "bdfghjklmnprstvz";
  static const llvm::StringRef VO = "aiou";
  std::vector<std::uint8_t> Result;
  if (!Str.startswith("pro"))
    return {};
  Str = Str.drop_front(3);
  while (!Str.empty()) {
    if (Str[0] != '-' || Str.size() < 4 || Str.size() == 5)
      return {};
    std::size_t Pos0 = CON.find(Str[1]);
    std::size_t Pos1 = VO.find(Str[2]);
    std::size_t Pos2 = CON.find(Str[3]);
    if (Pos0 == llvm::StringRef::npos || Pos1 == llvm::StringRef::npos ||
        Pos2 == llvm::StringRef::npos)
      return {};
    std::uint16_t Word = (Pos0 << 12) | (Pos1 << 10) | (Pos2 << 6);
    Result.push_back(Word >> 8);
    Str = Str.drop_front(4);
    if (Str.size() >= 2) {
      std::size_t Pos3 = VO.find(Str[0]);
      std::size_t Pos4 = CON.find(Str[1]);
      if (Pos3 == llvm::StringRef::npos || Pos4 == llvm::StringRef::npos)
        return {};
      Word |= (Pos3 << 4) | Pos4;
      Result.push_back(Word & 0xff);
      Str = Str.drop_front(2);
    }
  }
  return Result;
}

std::string proquintEncode(llvm::ArrayRef<std::uint8_t> Bytes) {
  // https://github.com/multiformats/multibase/blob/master/rfcs/PRO-QUINT.md

  // XXX: It isn't specified how to handle an odd number of bytes. This
  // implementation produces the minimum number of unambiguous chars, so there
  // will be a lone syllable.

  // It also isn't explicitly specified what happens if there are zero bytes.
  // This implementation produces "pro".

  static const char CON[] = {"bdfghjklmnprstvz"};
  static const char VO[] = {"aiou"};
  std::string Result = "pro";
  while (!Bytes.empty()) {
    std::uint16_t Word = Bytes[0] << 8;
    Bytes = Bytes.drop_front();
    bool FullWord = !Bytes.empty();
    if (FullWord) {
      Word |= Bytes[0];
      Bytes = Bytes.drop_front();
    }
    Result.push_back('-');
    Result.push_back(CON[Word >> 12]);
    Result.push_back(VO[(Word >> 10) & 3]);
    Result.push_back(CON[(Word >> 6) & 15]);
    if (FullWord) {
      Result.push_back(VO[(Word >> 4) & 3]);
      Result.push_back(CON[(Word >> 0) & 15]);
    }
  }
  return Result;
}

static const MultibaseCodec MULTIBASE_CODECS[] = {
    MultibaseCodec("base2", Multibase::Base2, '0', "01", std::nullopt),
    MultibaseCodec("base8", Multibase::Base8, '7', "01234567", std::nullopt),
    MultibaseCodec("base16", Multibase::Base16, 'f', "0123456789abcdef",
                   std::nullopt),
    MultibaseCodec("base16upper", Multibase::Base16Upper, 'F',
                   "0123456789ABCDEF", std::nullopt),
    MultibaseCodec("base32hex", Multibase::Base32Hex, 'v',
                   "0123456789abcdefghijklmnopqrstuv", std::nullopt),
    MultibaseCodec("base32hexupper", Multibase::Base32HexUpper, 'V',
                   "0123456789ABCDEFGHIJKLMNOPQRSTUV", std::nullopt),
    MultibaseCodec("base32hexpad", Multibase::Base32HexPad, 't',
                   "0123456789abcdefghijklmnopqrstuv", '='),
    MultibaseCodec("base32hexpadupper", Multibase::Base32HexPadUpper, 'T',
                   "0123456789ABCDEFGHIJKLMNOPQRSTUV", '='),
    MultibaseCodec("base32", Multibase::Base32, 'b',
                   "abcdefghijklmnopqrstuvwxyz234567", std::nullopt),
    MultibaseCodec("base32upper", Multibase::Base32Upper, 'B',
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", std::nullopt),
    MultibaseCodec("base32pad", Multibase::Base32Pad, 'c',
                   "abcdefghijklmnopqrstuvwxyz234567", '='),
    MultibaseCodec("base32padupper", Multibase::Base32PadUpper, 'C',
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", '='),
    MultibaseCodec("base32z", Multibase::Base32z, 'h',
                   "ybndrfg8ejkmcpqxot1uwisza345h769", std::nullopt),
    MultibaseCodec(
        "base64", Multibase::Base64, 'm',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
        std::nullopt),
    MultibaseCodec(
        "base64pad", Multibase::Base64Pad, 'M',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
        '='),
    MultibaseCodec(
        "base64url", Multibase::Base64Url, 'u',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
        std::nullopt),
    MultibaseCodec(
        "base64urlpad", Multibase::Base64UrlPad, 'U',
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
        '='),
};

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
  if (Text[0] == 'p') {
    auto Bytes = proquintDecode(Text);
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
  OrigBytes = OrigBytes.drop_back(Bytes.size());
  Result.Bytes.append(OrigBytes.begin(), OrigBytes.end());
  return Result;
}

std::string CID::asString(Multibase Base) const {
  for (const auto &Codec : MULTIBASE_CODECS)
    if (Base == Codec.ID)
      return Codec.Prefix + Codec.encode(asBytes());
  if (Base == Multibase::Proquint)
    return proquintEncode(asBytes());
  assert(false && "unsupported multibase");
  return asString(Multibase::Base32);
}

std::string CID::asString() const {
  for (const auto &Codec : MULTIBASE_CODECS)
    if (MultibaseOption == Codec.Name)
      return asString(Codec.ID);
  if (MultibaseOption == "proquint")
    return asString(Multibase::Proquint);
  return asString(Multibase::Base32);
}

CID::operator std::string() const { return asString(); }

std::ostream &memodb::operator<<(std::ostream &os, const CID &ref) {
  return os << std::string(ref);
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const CID &ref) {
  return os << std::string(ref);
}
