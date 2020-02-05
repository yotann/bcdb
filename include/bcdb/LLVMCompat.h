#ifndef BCDB_LLVMCOMPAT_H
#define BCDB_LLVMCOMPAT_H

#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Compiler.h>

#if LLVM_VERSION_MAJOR < 6
namespace llvm {
class tool_output_file;
using ToolOutputFile = tool_output_file;
} // end namespace llvm
#endif

#if LLVM_VERSION_MAJOR < 7
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Transforms/Utils/Cloning.h>
namespace llvm {
static std::unique_ptr<Module>
    LLVM_ATTRIBUTE_UNUSED CloneModule(const Module &M) {
  ValueToValueMapTy VMap;
  return CloneModule(&M, VMap);
}
static void LLVM_ATTRIBUTE_UNUSED WriteBitcodeToFile(const Module &M,
                                                     raw_ostream &Out) {
  WriteBitcodeToFile(&M, Out);
}
} // end namespace llvm
#endif

namespace bcdb {
static inline bool OptionHasCategory(llvm::cl::Option &O,
                                     llvm::cl::OptionCategory &C) {
#if LLVM_VERSION_MAJOR < 9
  return O.Category == &C;
#else
  for (llvm::cl::OptionCategory *C2 : O.Categories)
    if (C2 == &C)
      return true;
  return false;
#endif
}
} // end namespace bcdb

#endif // BCDB_LLVMCOMPAT_H
