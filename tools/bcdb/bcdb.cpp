#include "bcdb/BCDB.h"

#include <llvm/Bitcode/BitcodeWriter.h>
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
#include <memory>
#include <string>

using namespace bcdb;
using namespace llvm;

#if LLVM_VERSION_MAJOR <= 5
using ToolOutputFile = tool_output_file;
#endif

static cl::opt<std::string> Uri("uri", cl::Required,
                                cl::desc("URI of the database"),
                                cl::sub(*cl::AllSubCommands));

// bcdb add

static cl::SubCommand AddCommand("add", "Add a module");

static cl::opt<std::string> AddFilename(cl::Positional, cl::Required,
                                        cl::desc("<input bitcode file>"),
                                        cl::value_desc("filename"),
                                        cl::sub(AddCommand));

static cl::opt<std::string> AddName("name", cl::desc("Name of the new head"),
                                    cl::sub(AddCommand));

static int Add() {
  ExitOnError Err("bcdb add: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));

  LLVMContext Context;
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(AddFilename, Diag, Context);
  if (!M) {
    Diag.print("bcdb add", errs());
    return 1;
  }

  StringRef Name = AddName;
  if (Name.empty())
    Name = AddFilename;

  Err(db->Add(Name, std::move(M)));
  return 0;
}

// bcdb get

static cl::SubCommand GetCommand("get", "Retrieve a module");

static cl::opt<std::string> GetName("name", cl::Required,
                                    cl::desc("Name of the head to get"),
                                    cl::sub(GetCommand));

static cl::opt<std::string>
    GetOutputFilename("o", cl::desc("<output bitcode file>"), cl::init("-"),
                      cl::value_desc("filename"), cl::sub(GetCommand));

static cl::opt<bool> GetForce("f",
                              cl::desc("Enable binary output on terminals"),
                              cl::sub(GetCommand));

static int Get() {
  ExitOnError Err("bcdb get: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));
  std::unique_ptr<Module> M = Err(db->Get(GetName));

  std::error_code EC;
  ToolOutputFile Out(GetOutputFilename, EC, sys::fs::F_None);
  Err(errorCodeToError(EC));

  if (verifyModule(*M, &errs())) {
    return 1;
  }
  if (GetForce || !CheckBitcodeOutputToConsole(Out.os(), true)) {
#if LLVM_VERSION_MAJOR >= 7
    WriteBitcodeToFile(*M, Out.os());
#else
    WriteBitcodeToFile(M.get(), Out.os());
#endif
    Out.keep();
  }
  return 0;
}

// bcdb init

static cl::SubCommand InitCommand("init", "Initialize the database");

static int Init() {
  ExitOnError Err("bcdb init: ");
  Err(BCDB::Init(Uri));
  return 0;
}

// bcdb list-modules

static cl::SubCommand ListModulesCommand("list-modules",
                                         "List all modules in the database");

static int ListModules() {
  ExitOnError Err("bcdb list-modules: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));
  std::vector<std::string> names = Err(db->ListModules());
  for (auto &name : names) {
    outs() << name << "\n";
  }
  return 0;
}

// main

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  cl::ParseCommandLineOptions(argc, argv, "BCDB Tools");

  if (AddCommand) {
    return Add();
  } else if (GetCommand) {
    return Get();
  } else if (InitCommand) {
    return Init();
  } else if (ListModulesCommand) {
    return ListModules();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
