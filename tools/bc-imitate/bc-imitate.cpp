#include <string>
#include <utility>

#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Object/Binary.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Program.h>
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

static cl::SubCommand
    AnnotateCommand("annotate",
                    "Annotate a bitcode module with linking information");
static cl::SubCommand ClangCommand("clang", "Run Clang to link a module");
static cl::SubCommand
    ClangArgsCommand("clang-args",
                     "Determine Clang options for linking a module");

static cl::opt<std::string>
    InputFilenameBitcode(cl::Positional, cl::desc("<input bitcode file>"),
                         cl::value_desc("filename"), cl::sub(AnnotateCommand),
                         cl::sub(ClangCommand), cl::sub(ClangArgsCommand));

static cl::opt<std::string> BinaryFilename("binary",
                                           cl::desc("<input binary file>"),
                                           cl::value_desc("filename"),
                                           cl::sub(AnnotateCommand));

static cl::opt<std::string>
    OutputFilename("o", cl::desc("Override output filename"), cl::init("-"),
                   cl::value_desc("filename"), cl::sub(AnnotateCommand),
                   cl::sub(ClangCommand));

static cl::opt<std::string> OptLevel("O", cl::AlwaysPrefix,
                                     cl::desc("Optimization level"),
                                     cl::init("0"), cl::value_desc("level"),
                                     cl::sub(ClangCommand));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"),
                           cl::sub(AnnotateCommand));

static int Annotate() {
  ExitOnError Err("bc-imitate annotate: ");
  OwningBinary<Binary> OBinary = Err(createBinary(BinaryFilename));
  Binary &Binary = *OBinary.getBinary();

  LLVMContext Context;
  std::unique_ptr<Module> M;
  if (!InputFilenameBitcode.empty()) {
    SMDiagnostic Diag;
    M = parseIRFile(InputFilenameBitcode, Diag, Context);
    if (!M) {
      Diag.print("bc-imitate", errs());
      return 1;
    }
  } else {
    M = ExtractModuleFromBinary(Context, Binary);
    if (!M) {
      errs() << "can't extract bitcode from " << BinaryFilename << "\n";
      return 1;
    }
  }

  if (!AnnotateModuleWithBinary(*M, Binary)) {
    errs() << "unsupported binary file\n";
    return 1;
  }

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC, sys::fs::F_None);
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

static int Clang() {
  LLVMContext Context;
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(InputFilenameBitcode, Diag, Context);
  if (!M) {
    Diag.print("bc-imitate", errs());
    return 1;
  }

  // TODO: what if input is stdin?
  ExitOnError Err("bc-imitate clang: ");
  std::string OptArg = "-O" + OptLevel;
  std::vector<StringRef> Args = {
      "clang++", OptArg,        "-x", "ir", InputFilenameBitcode,
      "-o",      OutputFilename};
  auto Program = Err(errorOrToExpected(sys::findProgramByName(Args[0])));
  auto ClangArgs = ImitateClangArgs(*M);
  for (auto &Arg : ClangArgs)
    Args.push_back(Arg);
  return sys::ExecuteAndWait(Program, Args);
}

static int ClangArgs() {
  LLVMContext Context;
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(InputFilenameBitcode, Diag, Context);
  if (!M) {
    Diag.print("bc-imitate", errs());
    return 1;
  }

  for (auto Arg : ImitateClangArgs(*M))
    outs() << Arg << "\n";
  return 0;
}

int main(int argc, const char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  ReorganizeOptions([](cl::Option *O) {
    // Hide LLVM's options, since they're mostly irrelevant.
    O->setHiddenFlag(cl::Hidden);
    O->addSubCommand(*cl::AllSubCommands);
  });
  cl::ParseCommandLineOptions(argc, argv, "Imitate the native linker");

  if (AnnotateCommand) {
    return Annotate();
  } else if (ClangCommand) {
    return Clang();
  } else if (ClangArgsCommand) {
    return ClangArgs();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
