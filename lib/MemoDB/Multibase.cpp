#include "memodb/Multibase.h"

#include <cassert>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <optional>

using namespace memodb;

namespace {
template <unsigned BitsPerChar> class BitwiseBase : public Multibase {
  static const std::int8_t INVALID_VALUE = -1;
  static const std::int8_t PAD_VALUE = -2;

  llvm::StringRef Chars;
  std::optional<char> Pad;
  std::int8_t Values[256];

public:
  BitwiseBase(char Prefix, const char *Name, llvm::StringRef Chars,
              std::optional<char> Pad)
      : Multibase(Prefix, Name), Chars(Chars), Pad(Pad) {
    assert(Chars.size() == 1 << BitsPerChar);
    for (int i = 0; i < 256; i++)
      Values[i] = INVALID_VALUE;
    for (size_t i = 0; i < Chars.size(); i++)
      Values[(std::uint8_t)Chars[i]] = i;
    if (Pad)
      Values[(std::uint8_t)*Pad] = PAD_VALUE;
  }
  ~BitwiseBase() override {}

  std::optional<std::vector<std::uint8_t>>
  decodeWithoutPrefix(llvm::StringRef Str) const override;
  std::string
  encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> Bytes) const override;
};
} // end anonymous namespace

template <unsigned BitsPerChar>
std::optional<std::vector<std::uint8_t>>
BitwiseBase<BitsPerChar>::decodeWithoutPrefix(llvm::StringRef Str) const {
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

template <unsigned BitsPerChar>
std::string BitwiseBase<BitsPerChar>::encodeWithoutPrefix(
    llvm::ArrayRef<std::uint8_t> Bytes) const {
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

// https://github.com/multiformats/multibase/blob/master/rfcs/PRO-QUINT.md
namespace {
class ProquintBase : public Multibase {
public:
  ProquintBase(char Prefix, const char *Name) : Multibase(Prefix, Name) {}
  ~ProquintBase() override {}
  std::optional<std::vector<std::uint8_t>>
  decodeWithoutPrefix(llvm::StringRef Str) const override;
  std::string
  encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> Bytes) const override;
};
} // end anonymous namespace

std::optional<std::vector<std::uint8_t>>
ProquintBase::decodeWithoutPrefix(llvm::StringRef Str) const {
  static const llvm::StringRef CON = "bdfghjklmnprstvz";
  static const llvm::StringRef VO = "aiou";
  std::vector<std::uint8_t> Result;
  if (!Str.startswith("ro"))
    return {};
  Str = Str.drop_front(2);
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

std::string
ProquintBase::encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> Bytes) const {
  // XXX: It isn't specified how to handle an odd number of bytes. This
  // implementation produces the minimum number of unambiguous chars, so there
  // will be a lone syllable.

  // It also isn't explicitly specified what happens if there are zero bytes.
  // This implementation produces "pro" (after 'p' prefix is added).

  static const char CON[] = {"bdfghjklmnprstvz"};
  static const char VO[] = {"aiou"};
  std::string Result = "ro";
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

static const BitwiseBase<1> Base2('0', "base2", "01", std::nullopt);
static const BitwiseBase<3> Base8('7', "base8", "01234567", std::nullopt);
static const BitwiseBase<4> Base16('f', "base16", "0123456789abcdef",
                                   std::nullopt);
static const BitwiseBase<4> Base16Upper('F', "base16upper", "0123456789ABCDEF",
                                        std::nullopt);
static const BitwiseBase<5> Base32Hex('v', "base32hex",
                                      "0123456789abcdefghijklmnopqrstuv",
                                      std::nullopt);
static const BitwiseBase<5> Base32HexUpper('V', "base32hexupper",
                                           "0123456789ABCDEFGHIJKLMNOPQRSTUV",
                                           std::nullopt);
static const BitwiseBase<5>
    Base32HexPad('t', "base32hexpad", "0123456789abcdefghijklmnopqrstuv", '=');
static const BitwiseBase<5>
    Base32HexPadUpper('T', "base32hexpadupper",
                      "0123456789ABCDEFGHIJKLMNOPQRSTUV", '=');
static const BitwiseBase<5>
    Base32('b', "base32", "abcdefghijklmnopqrstuvwxyz234567", std::nullopt);
static const BitwiseBase<5> Base32Upper('B', "base32upper",
                                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
                                        std::nullopt);
static const BitwiseBase<5> Base32Pad('c', "base32pad",
                                      "abcdefghijklmnopqrstuvwxyz234567", '=');
static const BitwiseBase<5> Base32PadUpper('C', "base32padupper",
                                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
                                           '=');
static const BitwiseBase<5>
    Base32z('h', "base32z", "ybndrfg8ejkmcpqxot1uwisza345h769", std::nullopt);
static const BitwiseBase<6>
    Base64('m', "base64",
           "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
           std::nullopt);
static const BitwiseBase<6> Base64Pad(
    'M', "base64pad",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", '=');
static const BitwiseBase<6> Base64Url(
    'u', "base64url",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
    std::nullopt);
static const BitwiseBase<6> Base64UrlPad(
    'U', "base64urlpad",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", '=');
static const ProquintBase Proquint('p', "proquint");

const Multibase &Multibase::Base2 = ::Base2;
const Multibase &Multibase::Base8 = ::Base8;
const Multibase &Multibase::Base16 = ::Base16;
const Multibase &Multibase::Base16Upper = ::Base16Upper;
const Multibase &Multibase::Base32Hex = ::Base32Hex;
const Multibase &Multibase::Base32HexUpper = ::Base32HexUpper;
const Multibase &Multibase::Base32HexPad = ::Base32HexPad;
const Multibase &Multibase::Base32HexPadUpper = ::Base32HexPadUpper;
const Multibase &Multibase::Base32 = ::Base32;
const Multibase &Multibase::Base32Upper = ::Base32Upper;
const Multibase &Multibase::Base32Pad = ::Base32Pad;
const Multibase &Multibase::Base32PadUpper = ::Base32PadUpper;
const Multibase &Multibase::Base32z = ::Base32z;
const Multibase &Multibase::Base64 = ::Base64;
const Multibase &Multibase::Base64Pad = ::Base64Pad;
const Multibase &Multibase::Base64Url = ::Base64Url;
const Multibase &Multibase::Base64UrlPad = ::Base64UrlPad;
const Multibase &Multibase::Proquint = ::Proquint;

static const Multibase *ALL_BASES[] = {
    &Base2,        &Base8,          &Base16,       &Base16Upper,
    &Base32Hex,    &Base32HexUpper, &Base32HexPad, &Base32HexPadUpper,
    &Base32,       &Base32Upper,    &Base32Pad,    &Base32PadUpper,
    &Base32z,      &Base64,         &Base64Pad,    &Base64Url,
    &Base64UrlPad, &Proquint,
};

std::optional<std::vector<std::uint8_t>>
Multibase::decode(llvm::StringRef Str) {
  if (Str.empty())
    return {};
  for (const Multibase *Base : ALL_BASES)
    if (Str[0] == Base->Prefix)
      return Base->decodeWithoutPrefix(Str.drop_front());
  return {};
}

std::string Multibase::encode(llvm::ArrayRef<std::uint8_t> Bytes) const {
  return Prefix + encodeWithoutPrefix(Bytes);
}

const Multibase *Multibase::findByName(llvm::StringRef Name) {
  for (const Multibase *Base : ALL_BASES)
    if (Name == Base->Name)
      return Base;
  return nullptr;
}
