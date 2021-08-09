#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Threading.h>
#include <llvm/Support/raw_ostream.h>

#include "memodb/Evaluator.h"
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"
#include "outlining/Funcs.h"

using namespace llvm;
using namespace memodb;

llvm::cl::OptionCategory SmoutCategory("semantic outlining options");

static cl::SubCommand CandidatesCommand("candidates",
                                        "Generate outlineable candidates");

static cl::SubCommand
    CreateILPProblemCommand("create-ilp-problem",
                            "Create ILP problem for optimal outlining");

static cl::SubCommand EquivalenceCommand(
    "equivalence",
    "Check candidates for semantic equivalence (requires alive-worker)");

static cl::SubCommand
    ExtractCalleesCommand("extract-callees",
                          "Extract all outlinable callee functions");

static cl::SubCommand OptimizeCommand("optimize",
                                      "Optimize module with oulining");

static cl::SubCommand SolveGreedyCommand(
    "solve-greedy", "Calculate greedy solution to optimal outlining problem");

static cl::SubCommand
    WorkerCommand("worker",
                  "Start worker threads to evaluate jobs provided by server");

static cl::opt<std::string> Threads("j",
                                    cl::desc("Number of threads, or \"all\""),
                                    cl::cat(SmoutCategory),
                                    cl::sub(*cl::AllSubCommands));

static cl::opt<std::string>
    ModuleName("name", cl::Required, cl::desc("Name of the head to work on"),
               cl::cat(SmoutCategory), cl::sub(CandidatesCommand),
               cl::sub(CreateILPProblemCommand), cl::sub(EquivalenceCommand),
               cl::sub(ExtractCalleesCommand), cl::sub(OptimizeCommand),
               cl::sub(SolveGreedyCommand));

static cl::opt<std::string>
    StoreUriOrEmpty("store", cl::Optional, cl::desc("URI of the MemoDB store"),
                    cl::init(std::string(StringRef::withNullAsEmpty(
                        std::getenv("MEMODB_STORE")))),
                    cl::cat(SmoutCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetStoreUri() {
  if (StoreUriOrEmpty.empty()) {
    report_fatal_error(
        "You must provide a MemoDB store URI, such as "
        "sqlite:/tmp/example.bcdb, "
        "using the -store option or the MEMODB_STORE environment variable.");
  }
  return StoreUriOrEmpty;
}

static std::unique_ptr<Evaluator> createEvaluator() {
  // May be needed if smout.candidates is evaluated.
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  unsigned thread_count;
  if (Threads == "0") {
    thread_count = 0;
  } else {
    Optional<ThreadPoolStrategy> strategy_or_none =
        get_threadpool_strategy(Threads);
    if (!strategy_or_none)
      report_fatal_error("invalid number of threads");
    thread_count = strategy_or_none->compute_thread_count();
  }
  auto evaluator = Evaluator::create(GetStoreUri(), thread_count);
  evaluator->registerFunc(smout::candidates_version, &smout::candidates);
  evaluator->registerFunc(smout::candidates_total_version,
                          &smout::candidates_total);
  evaluator->registerFunc(smout::grouped_candidates_version,
                          &smout::grouped_candidates);
  evaluator->registerFunc(smout::extracted_callees_version,
                          &smout::extracted_callees);
  evaluator->registerFunc(smout::unique_callees_version,
                          &smout::unique_callees);
  evaluator->registerFunc(smout::ilp_problem_version, &smout::ilp_problem);
  evaluator->registerFunc(smout::greedy_solution_version,
                          &smout::greedy_solution);
  evaluator->registerFunc(smout::extracted_caller_version,
                          &smout::extracted_caller);
  evaluator->registerFunc(smout::optimized_version, &smout::optimized);
  evaluator->registerFunc(smout::equivalent_pairs_in_group_version,
                          &smout::equivalent_pairs_in_group);
  evaluator->registerFunc(smout::equivalent_pairs_version,
                          &smout::equivalent_pairs);
  return evaluator;
}

static Node getCandidatesOptions() { return Node(node_map_arg); }

// smout candidates

static int Candidates() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::candidates_total_version,
                                       getCandidatesOptions(), mod);
  llvm::outs() << "\nTotal candidates: " << *result << "\n";
  return 0;
}

// smout create-ilp-problem

static int CreateILPProblem() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::ilp_problem_version,
                                       getCandidatesOptions(), mod);
  llvm::outs() << result->as<StringRef>();
  return 0;
}

// smout equivalence

static int Equivalence() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::equivalent_pairs_version,
                                       getCandidatesOptions(), mod);
  llvm::outs() << "\nEquivalent pairs: " << *result << "\n";
  return 0;
}

// smout extract-callees

static int ExtractCallees() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::unique_callees_version,
                                       getCandidatesOptions(), mod);
  llvm::outs() << "\nUnique callee functions: " << *result << "\n";
  return 0;
}

// smout optimize

static int Optimize() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::optimized_version,
                                       getCandidatesOptions(), mod);
  llvm::outs() << Name(result.getCID()) << "\n";
  return 0;
}

// smout solve-greedy

static int SolveGreedy() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::greedy_solution_version,
                                       getCandidatesOptions(), mod);
  llvm::outs() << *result;
  return 0;
}

// smout worker

static int Worker() {
  using namespace std::chrono_literals;
  auto evaluator = createEvaluator();
  llvm::errs() << "connected\n";
  while (true) {
    std::this_thread::sleep_for(1s);
  }
  return 0;
}

// main

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  // Reorganize options into subcommands.
  ReorganizeOptions([](cl::Option *O) {
    if (OptionHasCategory(*O, SmoutCategory)) {
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
  } else if (CreateILPProblemCommand) {
    return CreateILPProblem();
  } else if (EquivalenceCommand) {
    return Equivalence();
  } else if (ExtractCalleesCommand) {
    return ExtractCallees();
  } else if (OptimizeCommand) {
    return Optimize();
  } else if (SolveGreedyCommand) {
    return SolveGreedy();
  } else if (WorkerCommand) {
    return Worker();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
