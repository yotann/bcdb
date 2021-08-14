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

static cl::SubCommand EvaluateCommand(
    "evaluate",
    "Evaluate an arbitrary func (if the func is built in to smout)");

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

static cl::opt<int> MinBenefit("min-benefit",
                               cl::desc("Outlined candidates must have this "
                                        "minimum estimated benefit, in bytes"),
                               cl::init(1), cl::cat(SmoutCategory),
                               cl::sub(OptimizeCommand),
                               cl::sub(SolveGreedyCommand));

static cl::opt<int>
    MinCallerSavings("min-caller-savings",
                     cl::desc("Outlined candidates must have this "
                              "minimum savings per caller, in bytes"),
                     cl::init(1), cl::cat(SmoutCategory),
                     cl::sub(OptimizeCommand), cl::sub(SolveGreedyCommand));

static cl::opt<bool> CompileAllCallers(
    "compile-all-callers",
    cl::desc("Compile all possible callers to determine actual sizes"),
    cl::init(false), cl::cat(SmoutCategory), cl::sub(OptimizeCommand),
    cl::sub(SolveGreedyCommand));

static cl::opt<bool>
    VerifyCallerSavings("verify-caller-savings",
                        cl::desc("Compile candidates selected for outlining to "
                                 "verify they are profitable"),
                        cl::init(false), cl::cat(SmoutCategory),
                        cl::sub(OptimizeCommand), cl::sub(SolveGreedyCommand));

static cl::opt<size_t> MaxArgs(
    "max-args",
    cl::desc(
        "Maximum number of arguments and return values for an outlined callee"),
    cl::init(10), cl::cat(SmoutCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<size_t> MaxAdjacent(
    "max-adjacent",
    cl::desc("Maximum candidate size generated using adjacent nodes"),
    cl::init(10), cl::cat(SmoutCategory), cl::sub(*cl::AllSubCommands));

static cl::opt<size_t>
    MaxNodes("max-nodes",
             cl::desc("Maximum candidate size generated using dependencies"),
             cl::init(50), cl::cat(SmoutCategory),
             cl::sub(*cl::AllSubCommands));

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
  smout::registerFuncs(*evaluator);
  return evaluator;
}

static Node getCandidatesOptions() {
  Node result(node_map_arg);
  if (MinBenefit != 1)
    result["min_benefit"] = Node(static_cast<int>(MinBenefit));
  if (MinCallerSavings != 1)
    result["min_caller_savings"] = Node(static_cast<int>(MinCallerSavings));
  if (MaxArgs != 10)
    result["max_args"] = Node(static_cast<size_t>(MaxArgs));
  if (MaxAdjacent != 10)
    result["max_adjacent"] = Node(static_cast<size_t>(MaxAdjacent));
  if (MaxNodes != 50)
    result["max_nodes"] = Node(static_cast<size_t>(MaxNodes));
  if (CompileAllCallers != false)
    result["compile_all_callers"] = Node(static_cast<bool>(CompileAllCallers));
  if (VerifyCallerSavings != false)
    result["verify_caller_savings"] =
        Node(static_cast<bool>(VerifyCallerSavings));
  return result;
}

// smout candidates

static int Candidates() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::grouped_candidates_version,
                                       getCandidatesOptions(), mod);
  unsigned total = 0, total_maybe_profitable = 0;
  unsigned group_count = result->size();
  unsigned group_count_singleton = 0;
  unsigned group_count_maybe_profitable = 0;
  unsigned largest_group_size = 0;
  std::string largest_group_name;
  for (const auto &item : result->map_range()) {
    size_t min_callee_size = item.value()["min_callee_size"].as<size_t>();
    size_t total_caller_savings =
        item.value()["total_caller_savings"].as<size_t>();
    unsigned num_members = item.value()["num_members"].as<unsigned>();
    total += num_members;
    if (num_members > largest_group_size) {
      largest_group_size = num_members;
      largest_group_name = item.key().str();
    }
    if (num_members == 1)
      group_count_singleton++;
    if (total_caller_savings > min_callee_size) {
      group_count_maybe_profitable += 1;
      total_maybe_profitable += num_members;
    }
  }
  llvm::outs() << "\nTotal groups: " << group_count << ", containing " << total
               << " candidates\n";
  llvm::outs() << "- singleton groups: " << group_count_singleton << "\n";
  llvm::outs() << "- other groups that can't possibly be profitable (according "
                  "to size estimates): "
               << (group_count - group_count_maybe_profitable -
                   group_count_singleton)
               << "\n";
  llvm::outs() << "- possibly profitable groups: "
               << group_count_maybe_profitable << ", containing "
               << total_maybe_profitable << " candidates\n";
  llvm::outs() << "Largest group (" << largest_group_size
               << " candidates): " << largest_group_name << "\n";
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
  NodeRef result = evaluator->evaluate(smout::grouped_refinements_version,
                                       getCandidatesOptions(), mod);
  llvm::outs() << "\nEquivalent pairs: " << *result << "\n";
  return 0;
}

// smout evaluate

static cl::opt<std::string> CallToEvaluate(cl::Positional, cl::Required,
                                           cl::desc("<call to evaluate>"),
                                           cl::value_desc("call"),
                                           cl::cat(SmoutCategory),
                                           cl::sub(EvaluateCommand));

static int Evaluate() {
  auto evaluator = createEvaluator();
  auto name_or_null = Name::parse(CallToEvaluate);
  if (!name_or_null || !std::holds_alternative<Call>(*name_or_null))
    report_fatal_error("invalid call URI");
  auto result = evaluator->evaluate(std::get<Call>(*name_or_null));
  llvm::outs() << result.getCID() << "\n";
  return 0;
}

// smout extract-callees

static int ExtractCallees() {
  auto evaluator = createEvaluator();
  CID mod = evaluator->getStore().resolve(Head(ModuleName));
  NodeRef result = evaluator->evaluate(smout::grouped_callees_version,
                                       getCandidatesOptions(), mod);
  unsigned total = 0, unique = 0, without_duplicates = 0;
  unsigned group_count = result->size();
  for (const auto &item : result->map_range()) {
    unsigned num_members = item.value()["num_members"].as<unsigned>();
    unsigned num_unique_callees =
        item.value()["num_unique_callees"].as<unsigned>();
    unsigned num_callees_without_duplicates =
        item.value()["num_callees_without_duplicates"].as<unsigned>();
    total += num_members;
    unique += num_unique_callees;
    without_duplicates += num_callees_without_duplicates;
  }
  llvm::outs() << "\nTotal extracted callees: " << total << " callees in "
               << group_count << " groups\n";
  llvm::outs() << "- " << unique << " unique callees\n";
  llvm::outs() << "- " << without_duplicates
               << " callees without any duplicates\n";
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
  } else if (EvaluateCommand) {
    return Evaluate();
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
