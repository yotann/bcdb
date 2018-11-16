#include "bcdb/BCDB.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>

using namespace bcdb;
using namespace llvm;

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

// bcdb init

static cl::SubCommand InitCommand("init", "Initialize the database");

static int Init() {
  ExitOnError Err("bcdb init: ");
  Err(BCDB::Init(Uri));
  return 0;
}

// main

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  cl::ParseCommandLineOptions(argc, argv, "BCDB Tools");

  if (AddCommand) {
    return Add();
  } else if (InitCommand) {
    return Init();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
