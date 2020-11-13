// XXX: You must include this file *after* including LLVM headers!

#ifndef BCDB_LLVMCOMPAT_H
#define BCDB_LLVMCOMPAT_H

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR < 6 && defined(LLVM_SUPPORT_TOOLOUTPUTFILE_H)
namespace llvm {
class tool_output_file;
using ToolOutputFile = tool_output_file;
} // end namespace llvm
#endif

#if LLVM_VERSION_MAJOR < 7 && defined(LLVM_TRANSFORMS_UTILS_CLONING_H)
namespace llvm {
static std::unique_ptr<Module>
    LLVM_ATTRIBUTE_UNUSED CloneModule(const Module &M) {
  ValueToValueMapTy VMap;
  return CloneModule(&M, VMap);
}
} // end namespace llvm
#endif

#if LLVM_VERSION_MAJOR < 7 && defined(LLVM_BITCODE_BITCODEWRITER_H)
namespace llvm {
static void LLVM_ATTRIBUTE_UNUSED WriteBitcodeToFile(const Module &M,
                                                     raw_ostream &Out) {
  WriteBitcodeToFile(&M, Out);
}
} // end namespace llvm
#endif

#if defined(LLVM_SUPPORT_COMMANDLINE_H)
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
#endif

#if defined(LLVM_ADT_BITVECTOR_H)
#include <iterator>
// Add full iterator support for BitVector::set_bits().
template <> struct std::iterator_traits<llvm::BitVector::set_iterator> {
  typedef void difference_type;
  typedef llvm::BitVector::size_type value_type;
  typedef value_type reference;
  typedef void pointer;
  typedef std::input_iterator_tag iterator_category;
};
#endif

#if defined(LLVM_IR_VALUE_H)
namespace bcdb {
static inline llvm::Value *stripPointerCastsAndAliases(llvm::Value *V) {
#if LLVM_VERSION_MAJOR < 10
  return V->stripPointerCasts();
#else
  return V->stripPointerCastsAndAliases();
#endif
}
} // end namespace bcdb
#endif

#endif // BCDB_LLVMCOMPAT_H
