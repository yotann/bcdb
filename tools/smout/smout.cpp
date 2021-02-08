#include <cstdlib>
#include <llvm/ADT/StringRef.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>
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
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <memory>
#include <string>

#include "bcdb/BCDB.h"
#include "bcdb/LLVMCompat.h"
#include "bcdb/Outlining/Candidates.h"
#include "bcdb/Outlining/Dependence.h"
#include "bcdb/Outlining/Extractor.h"
#include "memodb/memodb.h"

using namespace bcdb;
using namespace llvm;

static cl::SubCommand CandidatesCommand("candidates",
                                        "Generate outlineable candidates");

static cl::opt<std::string> ModuleName("name",
                                       cl::desc("Name of the head to work on"),
                                       cl::sub(CandidatesCommand));

static cl::opt<std::string> UriOrEmpty(
    "uri", cl::Optional, cl::desc("URI of the database"),
    cl::init(std::string(StringRef::withNullAsEmpty(std::getenv("BCDB_URI")))),
    cl::cat(BCDBCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetUri() {
  if (UriOrEmpty.empty()) {
    report_fatal_error(
        "You must provide a database URI, such as sqlite:/tmp/example.bcdb, "
        "using the -uri option or the BCDB_URI environment variable.");
  }
  return UriOrEmpty;
}

// smout candidates

static int Candidates() {
  ExitOnError Err("smout candidates: ");
  std::unique_ptr<BCDB> db = Err(BCDB::Open(GetUri()));
  for (auto &FuncId : Err(db->ListFunctionsInModule(ModuleName))) {
    auto M = Err(db->GetFunctionById(FuncId));
    Function *Def = nullptr;
    for (Function &F : *M) {
      if (!F.isDeclaration()) {
        if (Def) {
          Err(make_error<StringError>("multiple functions in function module",
                                      errc::invalid_argument));
        }
        Def = &F;
      }
    }

    legacy::PassManager PM;
    PM.add(new OutliningExtractorWrapperPass());
    PM.run(*M);

    // Make sure all functions are named so we can track them after saving.
    nameUnamedGlobals(*M);

    SmallVector<memodb_value, 8> NodesValues;
    SmallVector<std::string, 8> CalleeNames;
    SmallVector<std::string, 8> CallerNames;

    NamedMDNode *NMD = M->getNamedMetadata("smout.extracted.functions");
    if (NMD && NMD->getNumOperands()) {
      for (auto &Candidate :
           cast<MDNode>(*NMD->getOperand(0)->getOperand(1)).operands()) {
        // [Nodes, callee, caller]
        MDNode &Node = cast<MDNode>(*Candidate);
        ConstantDataSequential &NodesArray = cast<ConstantDataSequential>(
            *cast<ConstantAsMetadata>(*Node.getOperand(0)).getValue());
        Constant *Callee =
            cast<ConstantAsMetadata>(*Node.getOperand(1)).getValue();
        Constant *Caller =
            cast<ConstantAsMetadata>(*Node.getOperand(2)).getValue();
        if (Callee->isNullValue() || Caller->isNullValue())
          continue; // ignore unsupported sequences

        CalleeNames.push_back(Callee->getName().str());
        CallerNames.push_back(Caller->getName().str());
        memodb_value NodesValue = memodb_value::array();
        for (size_t i = 0; i < NodesArray.getNumElements(); i++)
          NodesValue.array_items().push_back(NodesArray.getElementAsInteger(i));
        NodesValues.push_back(std::move(NodesValue));
      }
    }

    Err(db->Add("__outlined", std::move(M)));

    memodb_value Module = db->get_db().get(db->get_db().head_get("__outlined"));
    memodb_value Functions = Module.map_items()["functions"];

    memodb_value Result = memodb_value::array();
    for (size_t i = 0; i < NodesValues.size(); i++) {
      Result.array_items().push_back(memodb_value::map({
          {"nodes", std::move(NodesValues[i])},
          {"callee",
           std::move(
               Functions.map_items()[memodb_value::bytes(CalleeNames[i])])},
          {"caller",
           std::move(
               Functions.map_items()[memodb_value::bytes(CallerNames[i])])},
      }));
    }

    db->get_db().call_set("smout.candidates", {memodb_ref{FuncId}},
                          db->get_db().put(Result));
  }
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
    } else {
      // Hide LLVM's options, since they're mostly irrelevant.
      O->setHiddenFlag(cl::Hidden);
      O->addSubCommand(*cl::AllSubCommands);
    }
  });
  cl::ParseCommandLineOptions(argc, argv, "Semantic Outlining");

  if (CandidatesCommand) {
    return Candidates();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
