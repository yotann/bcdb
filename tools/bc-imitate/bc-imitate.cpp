#include <string>
#include <utility>

#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Object/Binary.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb/LLVMCompat.h"
#include "bcdb/WholeProgram.h"

using namespace bcdb;
using namespace llvm;
using namespace llvm::object;

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::init("-"),
                                          cl::value_desc("filename"));

static cl::opt<std::string> BinaryFilename("binary",
                                           cl::desc("<input binary file>"),
                                           cl::value_desc("filename"));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::init("-"),
                                           cl::value_desc("filename"));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"));

int main(int argc, const char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  cl::ParseCommandLineOptions(argc, argv, "Make module imitate binary");

  LLVMContext Context;
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Diag, Context);
  if (!M) {
    Diag.print(argv[0], errs());
    return 1;
  }

  ExitOnError Err("bc-imitate: ");
  OwningBinary<Binary> OBinary = Err(createBinary(BinaryFilename));
  Binary &Binary = *OBinary.getBinary();

  if (!ImitateBinary(*M, Binary)) {
    errs() << "unsupported binary file\n";
    return 1;
  }

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC, sys::fs::F_None);
  Err(errorCodeToError(EC));
  if (verifyModule(*M, &errs())) {
    return 1;
  }
  if (Force || !CheckBitcodeOutputToConsole(Out.os(), true)) {
    WriteBitcodeToFile(*M, Out.os());
    Out.keep();
  }

  return 0;
}
