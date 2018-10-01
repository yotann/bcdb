#ifndef BCDB_SPLIT_H
#define BCDB_SPLIT_H

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <memory>

namespace bcdb {

void SplitModule(std::unique_ptr<llvm::Module> M,
                 llvm::function_ref<void(llvm::StringRef, llvm::StringRef,
                                         std::unique_ptr<llvm::Module> MPart)>
                     ModuleCallback);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
