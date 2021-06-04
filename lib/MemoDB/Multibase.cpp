#include "memodb/Multibase.h"

#include <cassert>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <optional>

using namespace memodb;

namespace {
template <unsigned bits_per_char> class BitwiseBase : public Multibase {
  static const std::int8_t InvalidValue = -1;
  static const std::int8_t PadValue = -2;

  llvm::StringRef chars_;
  std::optional<char> pad_;
  std::int8_t values_[256];

public:
  BitwiseBase(char prefix, const char *name, llvm::StringRef chars,
              std::optional<char> pad)
      : Multibase(prefix, name), chars_(chars), pad_(pad) {
    assert(chars.size() == 1 << bits_per_char);
    for (int i = 0; i < 256; i++)
      values_[i] = InvalidValue;
    for (size_t i = 0; i < chars.size(); i++)
      values_[(std::uint8_t)chars[i]] = i;
    if (pad)
      values_[(std::uint8_t)*pad] = PadValue;
  }
  ~BitwiseBase() override {}

  std::optional<std::vector<std::uint8_t>>
  decodeWithoutPrefix(llvm::StringRef str) const override;
  std::string
  encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> bytes) const override;
};
} // end anonymous namespace

template <unsigned bits_per_char>
std::optional<std::vector<std::uint8_t>>
BitwiseBase<bits_per_char>::decodeWithoutPrefix(llvm::StringRef str) const {
  // XXX: strings are accepted even if their pad bits are nonzero (e.g.,
  // "cafkqaab=" is equivalent to "cafkqaaa=").
  std::vector<std::uint8_t> Result;
  bool PadSeen = false;
  while (!str.empty()) {
    std::uint64_t Value = 0;
    size_t NumBits = 0, ValidBits = 0;
    do {
      Value <<= bits_per_char;
      NumBits += bits_per_char;
      if (str.empty() && pad_)
        return {}; // missing padding
      if (str.empty())
        continue;
      std::int8_t CharValue = values_[(std::uint8_t)str[0]];
      str = str.drop_front();
      if (CharValue == InvalidValue)
        return {}; // invalid char
      if (CharValue == PadValue && !ValidBits)
        return {}; // too much padding
      if (CharValue == PadValue) {
        PadSeen = true;
      } else {
        if (PadSeen)
          return {}; // chars after padding
        ValidBits += bits_per_char;
        Value |= CharValue;
      }
    } while (NumBits % 8);
    while (ValidBits >= 8) {
      ValidBits -= 8;
      NumBits -= 8;
      Result.push_back((Value >> NumBits) & 0xff);
    }
    if (ValidBits >= bits_per_char)
      return {}; // unused char
  }
  return Result;
}

template <unsigned bits_per_char>
std::string BitwiseBase<bits_per_char>::encodeWithoutPrefix(
    llvm::ArrayRef<std::uint8_t> bytes) const {
  std::string Result;
  while (!bytes.empty()) {
    uint64_t Value = 0;
    size_t NumBits = 0, ValidBits = 0;
    do {
      Value <<= 8;
      NumBits += 8;
      if (!bytes.empty()) {
        Value |= bytes[0];
        bytes = bytes.drop_front();
        ValidBits += 8;
      }
    } while (NumBits % bits_per_char);
    while (ValidBits) {
      ValidBits -= ValidBits > bits_per_char ? bits_per_char : ValidBits;
      NumBits -= bits_per_char;
      Result.push_back(chars_[(Value >> NumBits) & ((1 << bits_per_char) - 1)]);
    }
    while (pad_ && NumBits) {
      NumBits -= bits_per_char;
      Result.push_back(*pad_);
    }
  }
  return Result;
}

// https://github.com/multiformats/multibase/blob/master/rfcs/PRO-QUINT.md
namespace {
class ProquintBase : public Multibase {
public:
  ProquintBase(char prefix, const char *name) : Multibase(prefix, name) {}
  ~ProquintBase() override {}
  std::optional<std::vector<std::uint8_t>>
  decodeWithoutPrefix(llvm::StringRef str) const override;
  std::string
  encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> bytes) const override;
};
} // end anonymous namespace

std::optional<std::vector<std::uint8_t>>
ProquintBase::decodeWithoutPrefix(llvm::StringRef str) const {
  static const llvm::StringRef CON = "bdfghjklmnprstvz";
  static const llvm::StringRef VO = "aiou";
  std::vector<std::uint8_t> Result;
  if (!str.startswith("ro"))
    return {};
  str = str.drop_front(2);
  while (!str.empty()) {
    if (str[0] != '-' || str.size() < 4 || str.size() == 5)
      return {};
    std::size_t Pos0 = CON.find(str[1]);
    std::size_t Pos1 = VO.find(str[2]);
    std::size_t Pos2 = CON.find(str[3]);
    if (Pos0 == llvm::StringRef::npos || Pos1 == llvm::StringRef::npos ||
        Pos2 == llvm::StringRef::npos)
      return {};
    std::uint16_t Word = (Pos0 << 12) | (Pos1 << 10) | (Pos2 << 6);
    Result.push_back(Word >> 8);
    str = str.drop_front(4);
    if (str.size() >= 2) {
      std::size_t Pos3 = VO.find(str[0]);
      std::size_t Pos4 = CON.find(str[1]);
      if (Pos3 == llvm::StringRef::npos || Pos4 == llvm::StringRef::npos)
        return {};
      Word |= (Pos3 << 4) | Pos4;
      Result.push_back(Word & 0xff);
      str = str.drop_front(2);
    }
  }
  return Result;
}

std::string
ProquintBase::encodeWithoutPrefix(llvm::ArrayRef<std::uint8_t> bytes) const {
  // XXX: It isn't specified how to handle an odd number of bytes. This
  // implementation produces the minimum number of unambiguous chars, so there
  // will be a lone syllable.

  // It also isn't explicitly specified what happens if there are zero bytes.
  // This implementation produces "pro" (after 'p' prefix is added).

  static const char CON[] = {"bdfghjklmnprstvz"};
  static const char VO[] = {"aiou"};
  std::string Result = "ro";
  while (!bytes.empty()) {
    std::uint16_t Word = bytes[0] << 8;
    bytes = bytes.drop_front();
    bool FullWord = !bytes.empty();
    if (FullWord) {
      Word |= bytes[0];
      bytes = bytes.drop_front();
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

static const BitwiseBase<1> base2('0', "base2", "01", std::nullopt);
static const BitwiseBase<3> base8('7', "base8", "01234567", std::nullopt);
static const BitwiseBase<4> base16('f', "base16", "0123456789abcdef",
                                   std::nullopt);
static const BitwiseBase<4> base16upper('F', "base16upper", "0123456789ABCDEF",
                                        std::nullopt);
static const BitwiseBase<5> base32hex('v', "base32hex",
                                      "0123456789abcdefghijklmnopqrstuv",
                                      std::nullopt);
static const BitwiseBase<5> base32hexupper('V', "base32hexupper",
                                           "0123456789ABCDEFGHIJKLMNOPQRSTUV",
                                           std::nullopt);
static const BitwiseBase<5>
    base32hexpad('t', "base32hexpad", "0123456789abcdefghijklmnopqrstuv", '=');
static const BitwiseBase<5>
    base32hexpadupper('T', "base32hexpadupper",
                      "0123456789ABCDEFGHIJKLMNOPQRSTUV", '=');
static const BitwiseBase<5>
    base32('b', "base32", "abcdefghijklmnopqrstuvwxyz234567", std::nullopt);
static const BitwiseBase<5> base32upper('B', "base32upper",
                                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
                                        std::nullopt);
static const BitwiseBase<5> base32pad('c', "base32pad",
                                      "abcdefghijklmnopqrstuvwxyz234567", '=');
static const BitwiseBase<5> base32padupper('C', "base32padupper",
                                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
                                           '=');
static const BitwiseBase<5>
    base32z('h', "base32z", "ybndrfg8ejkmcpqxot1uwisza345h769", std::nullopt);
static const BitwiseBase<6>
    base64('m', "base64",
           "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
           std::nullopt);
static const BitwiseBase<6> base64pad(
    'M', "base64pad",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", '=');
static const BitwiseBase<6> base64url(
    'u', "base64url",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
    std::nullopt);
static const BitwiseBase<6> base64urlpad(
    'U', "base64urlpad",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", '=');
static const ProquintBase proquint('p', "proquint");

const Multibase &Multibase::base2 = ::base2;
const Multibase &Multibase::base8 = ::base8;
const Multibase &Multibase::base16 = ::base16;
const Multibase &Multibase::base16upper = ::base16upper;
const Multibase &Multibase::base32hex = ::base32hex;
const Multibase &Multibase::base32hexupper = ::base32hexupper;
const Multibase &Multibase::base32hexpad = ::base32hexpad;
const Multibase &Multibase::base32hexpadupper = ::base32hexpadupper;
const Multibase &Multibase::base32 = ::base32;
const Multibase &Multibase::base32upper = ::base32upper;
const Multibase &Multibase::base32pad = ::base32pad;
const Multibase &Multibase::base32padupper = ::base32padupper;
const Multibase &Multibase::base32z = ::base32z;
const Multibase &Multibase::base64 = ::base64;
const Multibase &Multibase::base64pad = ::base64pad;
const Multibase &Multibase::base64url = ::base64url;
const Multibase &Multibase::base64urlpad = ::base64urlpad;
const Multibase &Multibase::proquint = ::proquint;

static const Multibase *AllBases[] = {
    &base2,        &base8,          &base16,       &base16upper,
    &base32hex,    &base32hexupper, &base32hexpad, &base32hexpadupper,
    &base32,       &base32upper,    &base32pad,    &base32padupper,
    &base32z,      &base64,         &base64pad,    &base64url,
    &base64urlpad, &proquint,
};

std::optional<std::vector<std::uint8_t>>
Multibase::decode(llvm::StringRef str) {
  if (str.empty())
    return {};
  for (const Multibase *Base : AllBases)
    if (str[0] == Base->prefix)
      return Base->decodeWithoutPrefix(str.drop_front());
  return {};
}

std::string Multibase::encode(llvm::ArrayRef<std::uint8_t> bytes) const {
  return prefix + encodeWithoutPrefix(bytes);
}

const Multibase *Multibase::findByName(llvm::StringRef name) {
  for (const Multibase *Base : AllBases)
    if (name == Base->name)
      return Base;
  return nullptr;
}

void Multibase::eachBase(std::function<void(const Multibase &)> func) {
  for (const Multibase *Base : AllBases)
    func(*Base);
}
