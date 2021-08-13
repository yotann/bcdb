#include "memodb/ToolSupport.h"

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>

using namespace memodb;

static const char *argv0 = "<program>";

bool memodb::OptionHasCategory(llvm::cl::Option &O,
                               llvm::cl::OptionCategory &C) {
  for (llvm::cl::OptionCategory *C2 : O.Categories)
    if (C2 == &C)
      return true;
  return false;
}

InitTool::InitTool(int &argc, char **&argv) {
#if LLVM_VERSION_MAJOR >= 11
  llvm::setBugReportMsg(
      R"(
Fatal error! This is probably either a bug in BCDB, or you are using it incorrectly.
When you share this error, please include all parts of the error message.

)");
#endif

  // This is like llvm::InitLLVM, but it registers the handlers in a different
  // order, so the pretty stack trace goes on the bottom. This is important
  // because people who see an error message on the terminal have an annoying
  // tendency to copy the last few lines of it and ask that it be debugged
  // based on that information alone.
  //
  // Side note: LLVM prints items in the pretty stack trace and the backtrace
  // in opposite orders!
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  stack_printer.emplace(argc, argv);
  llvm::install_out_of_memory_new_handler();

#ifdef _WIN32
#error Need to copy the command-line argument conversion code from llvm::InitLLVM
#endif

  argv0 = argv[0];
}

llvm::StringRef memodb::getArgv0() { return argv0; }
