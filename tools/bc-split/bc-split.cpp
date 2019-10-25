#include <string>
#include <utility>

#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb/LLVMCompat.h"
#include "bcdb/Split.h"

using namespace bcdb;
using namespace llvm;

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

static cl::opt<std::string> OutputDirectory("o", cl::Required,
                                            cl::desc("<output directory>"),
                                            cl::value_desc("directory"));

namespace {
class DirSplitSaver : public SplitSaver {
  std::string Path;

public:
  DirSplitSaver(StringRef Path) : Path(Path) {
    std::error_code EC;
    std::string Filename;

    EC = sys::fs::create_directory(Path);
    if (EC) {
      errs() << EC.message() << '\n';
      exit(1);
    }
  }

  Error saveFunction(std::unique_ptr<Module> Module, StringRef Name) override {
    return saveModule("functions", Name, *Module);
  }

  Error saveRemainder(std::unique_ptr<Module> Module) override {
    return saveModule("remainder", "module", *Module);
  }

private:
  Error saveModule(StringRef Dir, StringRef File, Module &MPart) {
    std::error_code EC;

    EC = sys::fs::create_directories(Path + "/" + Dir);
    if (EC)
      return errorCodeToError(EC);

    std::string Filename = (Path + "/" + Dir + "/" + File + ".bc").str();
    ToolOutputFile Out(Filename, EC, sys::fs::F_None);
    if (EC)
      return errorCodeToError(EC);

    if (verifyModule(MPart, &errs()))
      return make_error<StringError>("could not verify module part",
                                     inconvertibleErrorCode());
    WriteBitcodeToFile(MPart, Out.os());
    Out.keep();
    return Error::success();
  }
};
} // end anonymous namespace

int main(int argc, const char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  cl::ParseCommandLineOptions(argc, argv, "Module splitting");

  LLVMContext Context;
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Diag, Context);
  if (!M) {
    Diag.print(argv[0], errs());
    return 1;
  }

  DirSplitSaver Saver(OutputDirectory);
  ExitOnError Err("bc-split: ");
  Err(SplitModule(std::move(M), Saver));

  return 0;
}
