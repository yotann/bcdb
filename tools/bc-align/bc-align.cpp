#include <string>
#include <utility>

#include <llvm/ADT/SmallVector.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb/AlignBitcode.h"
#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<input bitcode>"), cl::init("-"));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"));

static void WriteOutputFile(const SmallVectorImpl<char> &Buffer) {
  // Infer the output filename if needed.
  if (OutputFilename.empty()) {
    if (InputFilename == "-") {
      OutputFilename = "-";
    } else {
      StringRef IFN = InputFilename;
      OutputFilename = (IFN.endswith(".bc") ? IFN.drop_back(3) : IFN).str();
      OutputFilename += ".aligned.bc";
    }
  }

  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(OutputFilename, EC, sys::fs::F_None));
  if (EC) {
    errs() << EC.message() << '\n';
    exit(1);
  }

  if (Force || !CheckBitcodeOutputToConsole(Out->os(), true))
    Out->os().write(Buffer.data(), Buffer.size());

  // Declare success.
  Out->keep();
}

int main(int argc, const char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  cl::ParseCommandLineOptions(argc, argv, "bitcode aligner");

  ErrorOr<std::unique_ptr<MemoryBuffer>> MemBufOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename);
  if (std::error_code EC = MemBufOrErr.getError()) {
    errs() << "Error reading '" << InputFilename << "': " << EC.message();
    return 1;
  }
  std::unique_ptr<MemoryBuffer> MemBuf(std::move(MemBufOrErr.get()));
  SmallVector<char, 0> OutBuffer;
  OutBuffer.reserve(256 * 1024);

  ExitOnError Err("bc-align: ");
  Err(AlignBitcode(*MemBuf, OutBuffer));
  WriteOutputFile(OutBuffer);

  return 0;
}
