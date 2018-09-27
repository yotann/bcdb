#ifndef BCDB_ALIGNBITCODE_H
#define BCDB_ALIGNBITCODE_H

#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/MemoryBuffer.h>

namespace bcdb {

void AlignBitcode(llvm::MemoryBufferRef InBuffer,
                  llvm::SmallVectorImpl<char> &OutBuffer);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
