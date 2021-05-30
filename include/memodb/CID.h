#ifndef MEMODB_CID_H
#define MEMODB_CID_H

#include <cstdint>
#include <iosfwd>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <vector>

class memodb_value;

namespace memodb {

class CID {
private:
  std::vector<std::uint8_t> id_;
  enum { EMPTY, INLINE_RAW, INLINE_DAG, BLAKE2B_RAW, BLAKE2B_MERKLEDAG } type_;
  friend class ::memodb_value;
  static CID calculateFromContent(llvm::ArrayRef<std::uint8_t> Content,
                                  bool raw, bool noInline = false);

public:
  CID() : id_(), type_(EMPTY) {}
  CID(llvm::StringRef Text);
  static CID fromCID(llvm::ArrayRef<std::uint8_t> Bytes);
  static CID loadCIDFromSequence(llvm::ArrayRef<std::uint8_t> &Bytes);
  static CID fromBlake2BRaw(llvm::ArrayRef<std::uint8_t> Bytes);
  static CID fromBlake2BMerkleDAG(llvm::ArrayRef<std::uint8_t> Bytes);
  bool isInline() const { return type_ == INLINE_RAW || type_ == INLINE_DAG; }
  memodb_value asInline() const;
  std::vector<std::uint8_t> asCID() const;
  operator std::string() const;
  operator bool() const { return type_ != EMPTY; }
  bool operator<(const CID &other) const {
    if (type_ != other.type_)
      return type_ < other.type_;
    return id_ < other.id_;
  }
  bool operator==(const CID &other) const {
    return type_ == other.type_ && id_ == other.id_;
  }
};

std::ostream &operator<<(std::ostream &os, const CID &ref);
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const CID &ref);

} // end namespace memodb

#endif // MEMODB_CID_H
