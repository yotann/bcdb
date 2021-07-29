#include <cstdlib>
#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
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
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"

using namespace bcdb;
using namespace llvm;
using namespace memodb;

static cl::opt<std::string>
    StoreUriOrEmpty("store", cl::Optional, cl::desc("URI of the MemoDB store"),
                    cl::init(std::string(StringRef::withNullAsEmpty(
                        std::getenv("MEMODB_STORE")))),
                    cl::cat(BCDBCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetStoreUri() {
  if (StoreUriOrEmpty.empty()) {
    report_fatal_error(
        "You must provide a MemoDB store URI, such as "
        "sqlite:/tmp/example.bcdb, "
        "using the -store option or the MEMODB_STORE environment variable.");
  }
  return StoreUriOrEmpty;
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
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));

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
static cl::SubCommand GLCommand("gl", "Perform guided linking");
static cl::SubCommand MeltCommand("melt",
                                  "Load all functions into a single module");
static cl::SubCommand MergeCommand("merge", "Merge modules");
static cl::SubCommand MuxCommand("mux", "Mux modules");

static cl::opt<bool> DisableVerify("disable-verify",
                                   cl::desc("Don't verify the output module"),
                                   cl::sub(GetCommand),
                                   cl::sub(GetFunctionCommand),
                                   cl::sub(GLCommand), cl::sub(MeltCommand),
                                   cl::sub(MergeCommand), cl::sub(MuxCommand));

// FIXME: it's very confusing to have both GetHeadName and GetNameURI. We
// should deprecate GetHeadName.

static cl::opt<std::string> GetHeadName("name", cl::Optional,
                                        cl::desc("Name of the head to get"),
                                        cl::sub(GetCommand));

static cl::opt<std::string> GetNameURI(cl::Positional, cl::Optional,
                                       cl::desc("<name URI>"),
                                       cl::value_desc("uri"),
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
  if (GetForce || !CheckBitcodeOutputToConsole(OutputFile->os()))
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
  if (GetHeadName.empty() && GetNameURI.empty()) {
    errs() << "You must provide a name:\n";
    errs() << "  bcdb get --name=hello\n";
    errs() << " -or-\n";
    errs() << "  bcdb get /head/hello\n";
    return 1;
  }
  if (!GetHeadName.empty() && !GetNameURI.empty()) {
    errs() << "Too many names!\n";
    return 1;
  }
  std::optional<Name> name;
  name = !GetHeadName.empty() ? Head(GetHeadName) : Name::parse(GetNameURI);
  if (!name) {
    errs() << "Invalid name URI.\n";
    return 1;
  }
  std::unique_ptr<Store> store = Store::open(GetStoreUri());
  LLVMContext context;
  std::unique_ptr<Module> M = Err(getSplitModule(context, *store, *name));
  return WriteModule(*M);
}

static int GetFunction() {
  ExitOnError Err("bcdb get-function: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));
  std::unique_ptr<Module> M = Err(db->GetFunctionById(GetId));
  return WriteModule(*M);
}

static int Melt() {
  ExitOnError Err("bcdb melt: ");
  // Don't do the melt if we're just going to fail when writing the module.
  if (!Err(ShouldWriteModule()))
    return 0;
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));
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
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));
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
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));
  std::vector<std::string> names = Err(db->ListModules());
  for (auto &name : names) {
    outs() << name << "\n";
  }
  return 0;
}

// bcdb merge

static cl::list<std::string>
    MergeNames(cl::Positional, cl::OneOrMore, cl::desc("<module names>"),
               cl::sub(GLCommand), cl::sub(MergeCommand), cl::sub(MuxCommand));

static int Merge() {
  ExitOnError Err("bcdb merge: ");
  if (!Err(ShouldWriteModule()))
    return 0;
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));
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
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));
  std::vector<StringRef> Names;
  for (auto &Name : MergeNames)
    Names.push_back(Name);
  std::unique_ptr<Module> M = Err(db->Mux(Names));
  return WriteModule(*M);
}

static cl::opt<std::string>
    GLLibraryName("merged-name", cl::desc("<name of merged library>"),
                  cl::Required, cl::value_desc("filename"), cl::sub(GLCommand));

static cl::opt<std::string>
    GLWeakName("weak-name", cl::desc("<name of weak definitions library>"),
               cl::value_desc("filename"), cl::sub(GLCommand));

static cl::opt<std::string>
    GLOutputName("o", cl::desc("<output root directory>"), cl::Required,
                 cl::value_desc("directory"), cl::sub(GLCommand));

static int GL() {
  ExitOnError Err("bcdb gl: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetStoreUri()));
  std::vector<StringRef> Names;
  for (auto &Name : MergeNames)
    Names.push_back(Name);
  StringMap<std::unique_ptr<Module>> WrapperModules;
  std::unique_ptr<Module> WeakM;
  std::unique_ptr<Module> M = db->GuidedLinker(
      Names, WrapperModules, GLWeakName.empty() ? nullptr : &WeakM);

  auto SaveModule = [&](StringRef Path, Module &M) {
    if (!DisableVerify && verifyModule(M, &errs()))
      exit(1);
    auto OutPath = (GLOutputName + "/" + Path).str();

    std::error_code EC;
    EC = sys::fs::create_directories(sys::path::parent_path(OutPath));
    Err(errorCodeToError(EC));
    ToolOutputFile OutputFile(OutPath, EC, sys::fs::F_None);
    Err(errorCodeToError(EC));
    WriteBitcodeToFile(M, OutputFile.os());
    OutputFile.keep();
  };

  SaveModule(GLLibraryName, *M);
  if (!GLWeakName.empty())
    SaveModule(GLWeakName, *WeakM);
  for (auto &WrapperModule : WrapperModules)
    SaveModule(WrapperModule.first(), *WrapperModule.second);

  return 0;
}

// main

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  // Reorganize options into subcommands.
  ReorganizeOptions([](cl::Option *O) {
    if (OptionHasCategory(*O, BCDBCategory)) {
      O->addSubCommand(*cl::AllSubCommands);
    } else if (OptionHasCategory(*O, MergeCategory)) {
      O->addSubCommand(GLCommand);
      O->addSubCommand(MergeCommand);
      O->addSubCommand(MuxCommand);
    } else {
      // Hide LLVM's options, since they're mostly irrelevant.
      O->setHiddenFlag(cl::Hidden);
      O->addSubCommand(*cl::AllSubCommands);
    }
  });
  cl::ParseCommandLineOptions(argc, argv, "BCDB Tools");

  if (AddCommand) {
    return Add();
  } else if (GetCommand) {
    return Get();
  } else if (GetFunctionCommand) {
    return GetFunction();
  } else if (GLCommand) {
    return GL();
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
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
