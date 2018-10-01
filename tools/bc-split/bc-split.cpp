#include <string>
#include <utility>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb/Split.h"

using namespace bcdb;
using namespace llvm;

#if LLVM_VERSION_MAJOR <= 5
using ToolOutputFile = tool_output_file;
#endif

static cl::opt<std::string> InputFilename(cl::Positional, cl::Required,
                                          cl::desc("<input bitcode file>"));

static cl::opt<std::string> OutputDirectory(cl::Positional, cl::Required,
                                            cl::desc("<output directory>"));

int main(int argc, const char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "Module splitting");

  LLVMContext Context;
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  SplitModule(std::move(M), [&](StringRef dir, StringRef file,
                                std::unique_ptr<Module> MPart) {

    std::error_code EC;

    EC = sys::fs::create_directories(OutputDirectory + "/" + dir);
    if (EC) {
      errs() << EC.message() << '\n';
      exit(1);
    }

    std::string Filename = (OutputDirectory + "/" + dir + "/" + file).str();
    std::unique_ptr<ToolOutputFile> Out(
        new ToolOutputFile(Filename, EC, sys::fs::F_None));
    if (EC) {
      errs() << EC.message() << '\n';
      exit(1);
    }

    verifyModule(*MPart);
#if LLVM_VERSION_MAJOR >= 7
    WriteBitcodeToFile(*MPart, Out->os());
#else
    WriteBitcodeToFile(MPart.get(), Out->os());
#endif
    Out->keep();
  });

  return 0;
}
