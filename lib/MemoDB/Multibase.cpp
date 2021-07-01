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

static const BitwiseBase<4> base16('f', "base16", "0123456789abcdef",
                                   std::nullopt);
static const BitwiseBase<4> base16upper('F', "base16upper", "0123456789ABCDEF",
                                        std::nullopt);
static const BitwiseBase<5>
    base32('b', "base32", "abcdefghijklmnopqrstuvwxyz234567", std::nullopt);
static const BitwiseBase<5> base32upper('B', "base32upper",
                                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
                                        std::nullopt);
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

const Multibase &Multibase::base16 = ::base16;
const Multibase &Multibase::base16upper = ::base16upper;
const Multibase &Multibase::base32 = ::base32;
const Multibase &Multibase::base32upper = ::base32upper;
const Multibase &Multibase::base64 = ::base64;
const Multibase &Multibase::base64pad = ::base64pad;
const Multibase &Multibase::base64url = ::base64url;
const Multibase &Multibase::base64urlpad = ::base64urlpad;

static const Multibase *AllBases[] = {
    &base16, &base16upper, &base32,    &base32upper,
    &base64, &base64pad,   &base64url, &base64urlpad,
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
