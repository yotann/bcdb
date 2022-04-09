#ifndef BCDB_CONTEXT_H
#define BCDB_CONTEXT_H

#include <llvm/IR/LLVMContext.h>

namespace bcdb {

class Context {
  llvm::LLVMContext context;

public:
  Context();
  operator llvm::LLVMContext &();
  static void checkWarnings(const llvm::LLVMContext &context);
};

} // namespace bcdb

#endif // BCDB_CONTEXT_H
