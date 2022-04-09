// XXX: You must include this file *after* including LLVM headers!

#ifndef BCDB_LLVMCOMPAT_H
#define BCDB_LLVMCOMPAT_H

#include <llvm/Config/llvm-config.h>

#if defined(LLVM_IR_METADATA_H) && defined(LLVM_IR_MODULE_H)
namespace bcdb {
static inline void eraseModuleFlag(llvm::Module &M, llvm::StringRef Key) {
  llvm::SmallVector<llvm::Module::ModuleFlagEntry, 8> Flags;
  M.getModuleFlagsMetadata(Flags);
  if (Flags.empty())
    return;
  M.eraseNamedMetadata(M.getNamedMetadata("llvm.module.flags"));
  for (auto &Flag : Flags)
    if (Flag.Key->getString() != Key)
      M.addModuleFlag(Flag.Behavior, Flag.Key->getString(), Flag.Val);
}
} // end namespace bcdb
#endif

#endif // BCDB_LLVMCOMPAT_H
