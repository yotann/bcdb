#include <string>
#include <utility>

#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb/Split.h"

using namespace bcdb;
using namespace llvm;

#if LLVM_VERSION_MAJOR <= 5
using ToolOutputFile = tool_output_file;
#endif

static cl::opt<std::string> InputDirectory(cl::Positional, cl::Required,
                                           cl::desc("<input directory>"),
                                           cl::value_desc("directory"));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("<output bitcode file>"),
                                           cl::init("-"),
                                           cl::value_desc("filename"));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"));

namespace {
class DirSplitLoader : public SplitLoader {
  LLVMContext &Context;
  std::string Path;

public:
  DirSplitLoader(LLVMContext &Context, StringRef Path)
      : Context(Context), Path(Path) {}

  std::unique_ptr<llvm::Module> loadFunction(llvm::StringRef Name) override {
    return loadModule("functions", Name);
  }

  std::unique_ptr<llvm::Module> loadRemainder() override {
    return loadModule("remainder", "module");
  }

  std::unique_ptr<llvm::Module> loadModule(StringRef Dir, StringRef File) {
    std::string Filename = (Path + "/" + Dir + "/" + File).str();
    SMDiagnostic Err;
    std::unique_ptr<Module> M = parseIRFile(Filename, Err, Context);
    if (!M) {
      Err.print("bc-join", errs());
      exit(1);
    }
    return M;
  }
};
} // end anonymous namespace

int main(int argc, const char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  cl::ParseCommandLineOptions(argc, argv, "Module joining");

  LLVMContext Context;
  DirSplitLoader Loader(Context, InputDirectory);
  std::unique_ptr<Module> M = JoinModule(Loader);

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC, sys::fs::F_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  if (verifyModule(*M, &errs())) {
    return 1;
  }
  if (Force || !CheckBitcodeOutputToConsole(Out.os(), true)) {
#if LLVM_VERSION_MAJOR >= 7
    WriteBitcodeToFile(*M, Out.os());
#else
    WriteBitcodeToFile(M.get(), Out.os());
#endif
    Out.keep();
  }

  return 0;
}
