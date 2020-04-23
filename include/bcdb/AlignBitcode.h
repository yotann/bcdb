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

// Given a bitcode file followed by garbage, get the size of the actual
// bitcode. This only works correctly with some kinds of garbage (in
// particular, it will work if the bitcode file is followed by zeros, or if
// it's followed by another bitcode file).
size_t GetBitcodeSize(llvm::MemoryBufferRef Buffer);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
