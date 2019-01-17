#ifndef BCDB_ALIGNBITCODE_H
#define BCDB_ALIGNBITCODE_H

#include <llvm/ADT/SmallVector.h>

namespace llvm {
class Error;
class MemoryBufferRef;
class Module;
} // end namespace llvm

namespace bcdb {

llvm::Error AlignBitcode(llvm::MemoryBufferRef InBuffer,
                         llvm::SmallVectorImpl<char> &OutBuffer);

void WriteUnalignedModule(const llvm::Module &M,
                          llvm::SmallVectorImpl<char> &Buffer);

void WriteAlignedModule(const llvm::Module &M,
                        llvm::SmallVectorImpl<char> &Buffer);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
