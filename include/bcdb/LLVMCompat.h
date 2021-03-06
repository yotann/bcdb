// XXX: You must include this file *after* including LLVM headers!

#ifndef BCDB_LLVMCOMPAT_H
#define BCDB_LLVMCOMPAT_H

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR < 8 && defined(LLVM_SUPPORT_COMMANDLINE_H)
namespace llvm {
namespace cl {
static const FormattingFlags AlwaysPrefix = Prefix;
} // end namespace cl
} // end namespace llvm
#endif

#if LLVM_VERSION_MAJOR < 11
#define hasPassPointeeByValueAttr hasByValOrInAllocaAttr
#define isPassPointeeByValueArgument isByValOrInAllocaArgument
#endif

#if defined(LLVM_SUPPORT_COMMANDLINE_H)

#include <llvm/ADT/SmallVector.h>

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

template <typename F> static inline void ReorganizeOptions(F f) {
  // Reorganize options into subcommands.
  llvm::SmallVector<llvm::cl::Option *, 0> AllOptions;
  for (auto &I : llvm::cl::TopLevelSubCommand->OptionsMap)
    AllOptions.push_back(I.second);
  for (llvm::cl::Option *O : AllOptions) {
    if (O->isInAllSubCommands())
      continue; // no change (--help, --version, etc.)

    // In order for Option::addSubCommand() to take effect after the option has
    // been constructed, we need to remove the option before the change and
    // re-add it afterwards.
    O->removeArgument();
    f(O);
    O->addArgument();
  }
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

#if defined(LLVM_SUPPORT_SPECIALCASELIST_H)
#if LLVM_VERSION_MAJOR >= 10
#include <llvm/Support/VirtualFileSystem.h>
#endif
namespace bcdb {
static inline std::unique_ptr<llvm::SpecialCaseList>
createSpecialCaseList(const std::vector<std::string> &Paths) {
#if LLVM_VERSION_MAJOR < 10
  return llvm::SpecialCaseList::createOrDie(Paths);
#else
  return llvm::SpecialCaseList::createOrDie(Paths,
                                            *llvm::vfs::getRealFileSystem());
#endif
}
} // end namespace bcdb
#endif

#endif // BCDB_LLVMCOMPAT_H
