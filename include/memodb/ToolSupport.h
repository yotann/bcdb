#ifndef MEMODB_TOOL_SUPPORT_H
#define MEMODB_TOOL_SUPPORT_H

// Just some utilities for use in tools.

#include <optional>

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>

namespace memodb {

bool OptionHasCategory(llvm::cl::Option &O, llvm::cl::OptionCategory &C);

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

class InitTool {
  std::optional<llvm::PrettyStackTraceProgram> stack_printer;

public:
  InitTool(int &argc, char **&argv);
};

llvm::StringRef getArgv0();

} // end namespace memodb

#endif // MEMODB_TOOL_SUPPORT_H
