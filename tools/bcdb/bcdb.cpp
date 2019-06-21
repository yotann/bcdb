#include "bcdb/BCDB.h"
#include "bcdb/Split.h"

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

// bcdb get, get-function

static cl::SubCommand GetCommand("get", "Retrieve a module");
static cl::SubCommand GetFunctionCommand("get-function", "Retrieve a function");
static cl::SubCommand MeltCommand("melt",
                                  "Load all functions into a single module");

static cl::opt<std::string> GetName("name", cl::Required,
                                    cl::desc("Name of the head to get"),
                                    cl::sub(GetCommand));

static cl::opt<std::string> GetId("id", cl::Required,
                                  cl::desc("ID of the function to get"),
                                  cl::sub(GetFunctionCommand));

static cl::opt<std::string>
    GetOutputFilename("o", cl::desc("<output bitcode file>"), cl::init("-"),
                      cl::value_desc("filename"), cl::sub(GetCommand),
                      cl::sub(GetFunctionCommand), cl::sub(MeltCommand));

static cl::opt<bool> GetForce("f",
                              cl::desc("Enable binary output on terminals"),
                              cl::sub(GetCommand), cl::sub(GetFunctionCommand),
                              cl::sub(MeltCommand));

static Expected<bool> ShouldWriteModule() {
  std::error_code EC;
  ToolOutputFile Out(GetOutputFilename, EC, sys::fs::F_None);
  if (EC)
    return errorCodeToError(EC);
  return GetForce || !CheckBitcodeOutputToConsole(Out.os(), true);
}

static int WriteModule(Module &M) {
  ExitOnError Err("module write: ");
  std::error_code EC;
  ToolOutputFile Out(GetOutputFilename, EC, sys::fs::F_None);
  Err(errorCodeToError(EC));

  if (verifyModule(M, &errs())) {
    return 1;
  }
  if (GetForce || !CheckBitcodeOutputToConsole(Out.os(), true)) {
#if LLVM_VERSION_MAJOR >= 7
    WriteBitcodeToFile(M, Out.os());
#else
    WriteBitcodeToFile(&M, Out.os());
#endif
    Out.keep();
  }
  return 0;
}

static int Get() {
  ExitOnError Err("bcdb get: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));
  std::unique_ptr<Module> M = Err(db->Get(GetName));
  return WriteModule(*M);
}

static int GetFunction() {
  ExitOnError Err("bcdb get-function: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));
  std::unique_ptr<Module> M = Err(db->GetFunctionById(GetId));
  return WriteModule(*M);
}

static int Melt() {
  ExitOnError Err("bcdb melt: ");
  // Don't do the melt if we're just going to fail when writing the module.
  if (!Err(ShouldWriteModule()))
    return 0;
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));
  std::vector<std::string> names = Err(db->ListAllFunctions());
  int i = 0;
  Melter Melter(db->GetContext());
  for (auto &name : names) {
    auto MPart = Err(db->GetFunctionById(name));
    Err(Melter.Merge(std::move(MPart)));
    errs() << i++ << "," << names.size() << "," << name << "\n";
  }
  return WriteModule(Melter.GetModule());
}

// bcdb init

static cl::SubCommand InitCommand("init", "Initialize the database");

static int Init() {
  ExitOnError Err("bcdb init: ");
  Err(BCDB::Init(Uri));
  return 0;
}

// bcdb list-function-ids

static cl::SubCommand ListFunctionsCommand(
    "list-function-ids",
    "List function IDs in the database or a specific module");

static cl::opt<std::string> ListFunctionsName("name",
                                              cl::desc("Name of the module"),
                                              cl::init(""),
                                              cl::sub(ListFunctionsCommand));

static int ListFunctions() {
  ExitOnError Err("bcdb list-function-ids: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));
  std::vector<std::string> names;
  if (!ListFunctionsName.empty()) {
    names = Err(db->ListFunctionsInModule(ListFunctionsName));
  } else {
    names = Err(db->ListAllFunctions());
  }
  for (auto &name : names) {
    outs() << name << "\n";
  }
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


//Subcommand linked to DeleteHead
static cl::Subcommand DeleteHeadCommand("delete-head",
                                        "Delete head from the database head table");

// static cl::opt<std::string> DeleteHeadname(cl::Positional, cl::Required,
//                                         cl::desc("<head name>"),
//                                         cl::value_desc("name of head to delete"),
//                                         cl::sub(DeleteHeadCommand));

static cl::opt<std::string> AddName("name", cl::desc("name of head to delete"),
                                  cl::sub(DeleteHeadCommand));

static int DeleteHead(){
  ExitOnError Err("bcdb delete-head: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(Uri));
  StringRef Name = DeleteHeadname;
  Err(db->DeleteHead(Name));
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
  } else if (GetFunctionCommand) {
    return GetFunction();
  } else if (InitCommand) {
    return Init();
  } else if (ListFunctionsCommand) {
    return ListFunctions();
  } else if (ListModulesCommand) {
    return ListModules();
  } else if (MeltCommand) {
    return Melt();
  } else if (DeleteHeadCommand) {
    return DeleteHead();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
