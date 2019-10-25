#ifndef BCDB_LLVMCOMPAT_H
#define BCDB_LLVMCOMPAT_H

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR < 6
namespace llvm {
class tool_output_file;
using ToolOutputFile = tool_output_file;
} // end namespace llvm
#endif

#if LLVM_VERSION_MAJOR < 7
#include <llvm/Bitcode/BitcodeWriter.h>
namespace llvm {
void WriteBitcodeToFile(const Module &M, raw_ostream &Out) {
  WriteBitcodeToFile(&M, Out);
}
} // end namespace llvm
#endif

#endif // BCDB_LLVMCOMPAT_H
