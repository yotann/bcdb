#include <cstdlib>
#include <llvm/ADT/StringRef.h>
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

#include "bcdb/BCDB.h"
#include "bcdb/LLVMCompat.h"
#include "bcdb/Split.h"
#include "memodb/memodb.h"

using namespace bcdb;
using namespace llvm;

static cl::opt<std::string>
    UriOrEmpty("uri", cl::Optional, cl::desc("URI of the database"),
               cl::init(StringRef::withNullAsEmpty(std::getenv("BCDB_URI"))),
               cl::cat(BCDBCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetUri() {
  if (UriOrEmpty.empty()) {
    report_fatal_error(
        "You must provide a database URI, such as sqlite:/tmp/example.bcdb, "
        "using the -uri option or the BCDB_URI environment variable.");
  }
  return UriOrEmpty;
}

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
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));

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

// bcdb cp

static cl::SubCommand CpCommand("cp", "Copy a head");

static cl::opt<std::string> CpSource(cl::Positional, cl::Required,
                                     cl::value_desc("source"),
                                     cl::sub(CpCommand));

static cl::opt<std::string> CpDest(cl::Positional, cl::Required,
                                   cl::value_desc("dest"), cl::sub(CpCommand));

static int Cp() {
  ExitOnError Err("bcdb cp: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  memodb_ref value = db->get_db().head_get(CpSource);
  db->get_db().head_set(CpDest, value);
  return 0;
}

// bcdb delete

static cl::SubCommand DeleteCommand("delete", "Remove a module");

static cl::opt<std::string> DeleteHeadname("name",
                                           cl::desc("name of head to delete"),
                                           cl::sub(DeleteCommand));

static int Delete() {
  ExitOnError Err("bcdb delete: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  StringRef Name = DeleteHeadname;
  Err(db->Delete(Name));
  return 0;
}

// bcdb get, get-function

static cl::SubCommand GetCommand("get", "Retrieve a module");
static cl::SubCommand GetFunctionCommand("get-function", "Retrieve a function");
static cl::SubCommand MeltCommand("melt",
                                  "Load all functions into a single module");
static cl::SubCommand MergeCommand("merge", "Merge modules");
static cl::SubCommand MuxCommand("mux", "Mux modules");
static cl::SubCommand Mux2Command("mux2", "Mux modules (separate-ELF version)");

static cl::opt<bool> DisableVerify("disable-verify",
                                   cl::desc("Don't verify the output module"),
                                   cl::sub(GetCommand),
                                   cl::sub(GetFunctionCommand),
                                   cl::sub(MeltCommand), cl::sub(MergeCommand),
                                   cl::sub(MuxCommand));

static cl::opt<std::string> GetName("name", cl::Required,
                                    cl::desc("Name of the head to get"),
                                    cl::sub(GetCommand));

static cl::opt<std::string> GetId("id", cl::Required,
                                  cl::desc("ID of the function to get"),
                                  cl::sub(GetFunctionCommand));

static cl::opt<std::string>
    GetOutputFilename("o", cl::desc("<output bitcode file>"), cl::init("-"),
                      cl::value_desc("filename"), cl::sub(GetCommand),
                      cl::sub(GetFunctionCommand), cl::sub(MeltCommand),
                      cl::sub(MergeCommand), cl::sub(MuxCommand));

static cl::opt<bool> GetForce("f",
                              cl::desc("Enable binary output on terminals"),
                              cl::sub(GetCommand), cl::sub(GetFunctionCommand),
                              cl::sub(MeltCommand), cl::sub(MergeCommand),
                              cl::sub(MuxCommand));

static std::unique_ptr<ToolOutputFile> OutputFile;

static Expected<bool> ShouldWriteModule() {
  if (OutputFile)
    return true;
  std::error_code EC;
  OutputFile =
      std::make_unique<ToolOutputFile>(GetOutputFilename, EC, sys::fs::F_None);
  if (EC)
    return errorCodeToError(EC);
  if (GetForce || !CheckBitcodeOutputToConsole(OutputFile->os(), true))
    return true;
  OutputFile.reset();
  return false;
}

static int WriteModule(Module &M) {
  ExitOnError Err("module write: ");
  if (!DisableVerify && verifyModule(M, &errs())) {
    return 1;
  }
  if (Err(ShouldWriteModule())) {
    WriteBitcodeToFile(M, OutputFile->os());
    OutputFile->keep();
  }
  OutputFile.reset();
  return 0;
}

static int Get() {
  ExitOnError Err("bcdb get: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  std::unique_ptr<Module> M = Err(db->Get(GetName));
  return WriteModule(*M);
}

static int GetFunction() {
  ExitOnError Err("bcdb get-function: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  std::unique_ptr<Module> M = Err(db->GetFunctionById(GetId));
  return WriteModule(*M);
}

static int Melt() {
  ExitOnError Err("bcdb melt: ");
  // Don't do the melt if we're just going to fail when writing the module.
  if (!Err(ShouldWriteModule()))
    return 0;
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
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

// bcdb head-get

static cl::SubCommand HeadGetCommand("head-get", "Look up value ID of a head");

static cl::list<std::string> HeadGetNames(cl::Positional, cl::OneOrMore,
                                          cl::desc("<head names>"),
                                          cl::sub(HeadGetCommand));

static int HeadGet() {
  ExitOnError Err("bcdb head-get: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  for (StringRef Name : HeadGetNames) {
    memodb_ref value = db->get_db().head_get(Name);
    outs() << StringRef(value) << "\n";
  }
  return 0;
}

// bcdb init

static cl::SubCommand InitCommand("init", "Initialize the database");

static int Init() {
  ExitOnError Err("bcdb init: ");
  Err(BCDB::Init(GetUri()));
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
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
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
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  std::vector<std::string> names = Err(db->ListModules());
  for (auto &name : names) {
    outs() << name << "\n";
  }
  return 0;
}

// bcdb merge

static cl::list<std::string> MergeNames(cl::Positional, cl::OneOrMore,
                                        cl::desc("<module names>"),
                                        cl::sub(MergeCommand),
                                        cl::sub(MuxCommand),
                                        cl::sub(Mux2Command));

static int Merge() {
  ExitOnError Err("bcdb merge: ");
  if (!Err(ShouldWriteModule()))
    return 0;
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  std::vector<StringRef> Names;
  for (auto &Name : MergeNames)
    Names.push_back(Name);
  std::unique_ptr<Module> M = Err(db->Merge(Names));
  return WriteModule(*M);
}

static int Mux() {
  ExitOnError Err("bcdb mux: ");
  if (!Err(ShouldWriteModule()))
    return 0;
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  std::vector<StringRef> Names;
  for (auto &Name : MergeNames)
    Names.push_back(Name);
  std::unique_ptr<Module> M = Err(db->Mux(Names));
  return WriteModule(*M);
}

static int Mux2() {
  ExitOnError Err("bcdb mux2: ");
  if (!Err(ShouldWriteModule()))
    return 0;
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  std::vector<StringRef> Names;
  for (auto &Name : MergeNames)
    Names.push_back(Name);
  db->Mux2(Names);
  return 0;
}

// Calls: bcdb cache, evaluate, invalidate

static cl::SubCommand CacheCommand("cache", "Cache function call");
static cl::SubCommand EvaluateCommand("evaluate", "Evaluate function call");
static cl::SubCommand InvalidateCommand("invalidate",
                                        "Invalidate cached function calls");

static cl::opt<std::string> FuncResult("result", cl::Required,
                                       cl::desc("function result ID"),
                                       cl::sub(CacheCommand));
static cl::opt<std::string>
    FuncName(cl::Positional, cl::Required, cl::desc("<function name>"),
             cl::value_desc("func"), cl::sub(CacheCommand),
             cl::sub(EvaluateCommand), cl::sub(InvalidateCommand));
static cl::list<std::string> FuncArgs(cl::Positional, cl::OneOrMore,
                                      cl::desc("<arguments>"),
                                      cl::sub(CacheCommand),
                                      cl::sub(EvaluateCommand));

static int Cache() {
  ExitOnError Err("bcdb cache: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));

  std::vector<memodb_ref> args;
  for (const auto &arg_id : FuncArgs) {
    args.push_back(memodb_ref(arg_id));
  }

  db->get_db().call_set(FuncName, args, memodb_ref(FuncResult));
  return 0;
}

static int Evaluate() {
  ExitOnError Err("bcdb evaluate: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));

  std::vector<memodb_ref> args;
  for (const auto &arg_id : FuncArgs) {
    args.push_back(memodb_ref(arg_id));
  }

  memodb_ref result = db->get_db().call_get(FuncName, args);
  if (!result) {
    report_fatal_error("Can't evaluate function " + FuncName);
  }

  outs() << llvm::StringRef(result) << "\n";
  return 0;
}

static int Invalidate() {
  ExitOnError Err("bcdb invalidate: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));

  db->get_db().call_invalidate(FuncName);
  return 0;
}

// bcdb refs

static cl::SubCommand RefsCommand("refs", "List references to a value");

static cl::opt<std::string> RefsValue(cl::Positional, cl::Required,
                                      cl::desc("<value ID>"),
                                      cl::sub(RefsCommand));

static int Refs() {
  ExitOnError Err("bcdb refs: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));

  memodb_ref ref(RefsValue);
  for (const auto &path : db->get_db().list_paths_to(ref)) {
    outs() << "heads";
    for (const auto &item : path) {
      outs() << "[" << item << "]";
    }
    outs() << "\n";
  }
  return 0;
}

// main

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  for (auto &I : cl::TopLevelSubCommand->OptionsMap) {
    if (OptionHasCategory(*I.second, cl::GeneralCategory)) {
      // Hide LLVM's options, since they're mostly irrelevant.
      I.second->setHiddenFlag(cl::Hidden);
      I.second->addSubCommand(*cl::AllSubCommands);
    } else if (OptionHasCategory(*I.second, BCDBCategory)) {
      I.second->addSubCommand(*cl::AllSubCommands);
    } else if (OptionHasCategory(*I.second, MergeCategory)) {
      I.second->addSubCommand(MergeCommand);
      I.second->addSubCommand(MuxCommand);
      I.second->addSubCommand(Mux2Command);
    } else {
      // no change (--help, --version, etc.)
    }
  }
  cl::ParseCommandLineOptions(argc, argv, "BCDB Tools");

  if (AddCommand) {
    return Add();
  } else if (CacheCommand) {
    return Cache();
  } else if (CpCommand) {
    return Cp();
  } else if (DeleteCommand) {
    return Delete();
  } else if (EvaluateCommand) {
    return Evaluate();
  } else if (GetCommand) {
    return Get();
  } else if (GetFunctionCommand) {
    return GetFunction();
  } else if (HeadGetCommand) {
    return HeadGet();
  } else if (InitCommand) {
    return Init();
  } else if (InvalidateCommand) {
    return Invalidate();
  } else if (ListFunctionsCommand) {
    return ListFunctions();
  } else if (ListModulesCommand) {
    return ListModules();
  } else if (MeltCommand) {
    return Melt();
  } else if (MergeCommand) {
    return Merge();
  } else if (MuxCommand) {
    return Mux();
  } else if (Mux2Command) {
    return Mux2();
  } else if (RefsCommand) {
    return Refs();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
