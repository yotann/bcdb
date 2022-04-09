#ifndef MEMODB_CBORENCODER_H
#define MEMODB_CBORENCODER_H

#include <cstdint>
#include <vector>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>

#include "NodeVisitor.h"

namespace memodb {

/// Visitor that encodes Nodes in CBOR format.
///
/// https://www.rfc-editor.org/rfc/rfc8949.html
class CBOREncoder : public NodeVisitor {
public:
  CBOREncoder(std::vector<std::uint8_t> &out);
  virtual ~CBOREncoder();

  /// Return whether the already-encoded CBOR includes links (CIDs, tag 42).
  bool hasLinks() const;

  /// Return whether the already-encoded CBOR complies with the DAG-CBOR limits.
  ///
  /// https://github.com/ipld/specs/blob/master/block-layer/codecs/dag-cbor.md
  bool isValidDAGCBOR() const;

  /// Encode the given value in the given IEEE 754 binary float format.
  ///
  /// \param result The result, as an integer.
  /// \param value The value to encode.
  /// \param total_size The total number of bits in the desired format.
  /// \param mantissa_size The number of mantissa bits in the desired format.
  /// \param exponent_bias The exponent value that represents an exponent of 0.
  /// \return True if the conversion is exact; false if rounding was required.
  static bool encodeFloat(std::uint64_t &result, double value, int total_size,
                          int mantissa_size, int exponent_bias);

  /// Encode the head for a CBOR data item.
  ///
  /// https://www.rfc-editor.org/rfc/rfc8949.html#name-specification-of-the-cbor-e
  ///
  /// \param major_type The major type (0...7).
  /// \param argument The argument value.
  /// \param force_additional If nonzero, forces the use of a specific
  ///     "additional information" value, encoding the argument with a specific
  ///     number of bytes. If zero, the shortest possible encoding is used.
  void encodeHead(int major_type, std::uint64_t argument,
                  int force_additional = 0);

  void visitNull() override;
  void visitBoolean(bool value) override;
  void visitUInt64(std::uint64_t value) override;
  void visitInt64(std::int64_t value) override;
  void visitFloat(double value) override;
  void visitString(llvm::StringRef value) override;
  void visitBytes(BytesRef value) override;
  void visitLink(const Link &value) override;
  void startList(const Node::List &value) override;
  void startMap(const Node::Map &value) override;

protected:
  std::vector<std::uint8_t> &out;
  bool has_links = false;
  bool not_dag_cbor = false;
};

} // end namespace memodb

#endif // MEMODB_CBORENCODER_H
