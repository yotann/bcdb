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
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb/Split.h"
#include "memodb/ToolSupport.h"

using namespace bcdb;
using namespace llvm;
using namespace memodb;

static cl::OptionCategory Category("Joining options");

static cl::opt<std::string> InputDirectory(cl::Positional, cl::Required,
                                           cl::desc("<input directory>"),
                                           cl::value_desc("directory"),
                                           cl::cat(Category));

static cl::opt<std::string>
    OutputFilename("o", cl::desc("<output bitcode file>"), cl::init("-"),
                   cl::value_desc("filename"), cl::cat(Category));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"),
                           cl::cat(Category));

static std::unique_ptr<llvm::Module> loadModule(LLVMContext &Context,
                                                StringRef Filename) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(Filename, Err, Context);
  if (!M) {
    Err.print("bc-join", errs());
    exit(1);
  }
  return M;
}

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  HideUnrelatedOptions(Category, *cl::TopLevelSubCommand);
  cl::ParseCommandLineOptions(argc, argv, "Module joining");

  LLVMContext Context;
  ExitOnError Err("bc-join: ");
  std::error_code EC;

  auto M = loadModule(Context, InputDirectory + "/remainder/module.bc");
  Joiner Joiner(*M);
  for (sys::fs::directory_iterator I(InputDirectory + "/functions", EC), IE;
       I != IE && !EC; I.increment(EC)) {
    if (StringRef(I->path()).endswith(".bc")) {
      auto MPart = loadModule(Context, I->path());
      Joiner.JoinGlobal(sys::path::stem(I->path()), std::move(MPart));
    }
  }
  Err(errorCodeToError(EC));
  Joiner.Finish();

  ToolOutputFile Out(OutputFilename, EC, sys::fs::OF_None);
  Err(errorCodeToError(EC));

  if (verifyModule(*M, &errs())) {
    return 1;
  }
  if (Force || !CheckBitcodeOutputToConsole(Out.os())) {
    WriteBitcodeToFile(*M, Out.os());
    Out.keep();
  }

  return 0;
}
