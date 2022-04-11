#include "memodb/ToolSupport.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb-version.h"

using namespace memodb;

static const char *argv0 = "<program>";

bool memodb::OptionHasCategory(llvm::cl::Option &O,
                               llvm::cl::OptionCategory &C) {
  for (llvm::cl::OptionCategory *C2 : O.Categories)
    if (C2 == &C)
      return true;
  return false;
}

static void printVersion(llvm::raw_ostream &os) {
  os << "BCDB (https://github.com/yotann/bcdb):\n  ";
  os << "revision " << REVISION_DESCRIPTION_FINAL << "\n  ";
  os << "using LLVM " << LLVM_VERSION_STRING << "\n  ";
  os << "enabled features:";
#ifndef NDEBUG
  os << " assertions";
#endif
#if BCDB_WITH_ROCKSDB
  os << " RocksDB";
#endif
  os << "\n  disabled features:";
#ifdef NDEBUG
  os << " assertions";
#endif
#if !BCDB_WITH_ROCKSDB
  os << " RocksDB";
#endif
  os << "\n";
}

InitTool::InitTool(int &argc, char **&argv) {
  llvm::setBugReportMsg(
      R"(
Fatal error! This is probably either a bug in BCDB, or you are using it incorrectly.
When you share this error, please include all parts of the error message.
See docs/memodb/debugging.md for debugging suggestions.

)");

  llvm::cl::SetVersionPrinter(printVersion);

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
