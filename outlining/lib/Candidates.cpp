#include "outlining/Candidates.h"

#include <string>
#include <tuple>

#include <llvm/IR/InstrTypes.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FormatVariadic.h>

#include "bcdb/LLVMCompat.h"
#include "outlining/Dependence.h"
#include "outlining/Extractor.h"
#include "outlining/SizeModel.h"

using namespace bcdb;
using namespace llvm;

static cl::list<std::string> OutlineOnly("outline-only", cl::ZeroOrMore,
                                         cl::desc("Specify nodes to outline"),
                                         cl::value_desc("func:node,node,..."));

static cl::opt<size_t>
    OutlineMaxAdjacent("outline-max-adjacent", cl::init(10),
                       cl::desc("Maximum number of unrelated adjacent "
                                "instructions to outline together."));

static cl::opt<size_t> OutlineMaxArgs(
    "outline-max-args", cl::init(10),
    cl::desc("Maximum number of arguments and return values for the callee."));

static cl::opt<size_t>
    OutlineMaxNodes("outline-max-nodes", cl::init(50),
                    cl::desc("Maximum number of instructions to outline."));

static cl::opt<bool> OutlineUnprofitable(
    "outline-unprofitable",
    cl::desc(
        "Outline every possible sequence, even if it seems unprofitable."));

OutliningCandidatesOptions OutliningCandidatesOptions::getFromCommandLine() {
  OutliningCandidatesOptions options;
  options.max_adjacent = OutlineMaxAdjacent;
  options.max_nodes = OutlineMaxNodes;
  options.max_args = OutlineMaxArgs;
  return options;
}

OutliningCandidates::OutliningCandidates(
    Function &F, OutliningDependenceResults &OutDep,
    const SizeModelResults *size_model,
    const OutliningCandidatesOptions &options)
    : F(F), OutDep(OutDep), size_model(size_model), options(options) {
  if (!OutlineOnly.empty()) {
    for (StringRef chosen : OutlineOnly) {
      if (chosen.contains(':')) {
        StringRef func_name;
        std::tie(func_name, chosen) = chosen.split(':');
        if (func_name != F.getName())
          continue;
      }
      Candidate candidate;
      while (!chosen.empty()) {
        StringRef span;
        std::tie(span, chosen) = chosen.split(',');
        StringRef first_str, last_str;
        std::tie(first_str, last_str) = span.split('-');
        size_t first = 0;
        first_str.getAsInteger(10, first);
        size_t last = first;
        last_str.getAsInteger(10, last);
        for (size_t i = first; i <= last; ++i)
          addNode(candidate, i);
      }
      if (!OutDep.isOutlinable(candidate.bv))
        report_fatal_error("Specified nodes cannot be outlined",
                           /* gen_crash_diag */ false);
      Candidates.emplace_back(std::move(candidate));
    }
    return;
  }

  OutDep.computeTransitiveClosures();

  for (BasicBlock &block : F) {
    if (!OutDep.NodeIndices.count(&block))
      continue; // unreachable
    auto first = OutDep.NodeIndices[block.getFirstNonPHI()];
    auto last = OutDep.NodeIndices[block.getTerminator()];

    // Don't bother outlining unconditional branches, unless part of a loop.
    if (block.getTerminator()->getOpcode() == Instruction::Br &&
        block.getSingleSuccessor() &&
        OutDep.DT.dominates(&block, block.getSingleSuccessor()))
      last--;

    for (auto i = first; i < last; ++i) {
      generateCandidatesEndingAt(i);
    }
  }
}

void OutliningCandidates::generateCandidatesEndingAt(size_t i) {
  // candidate.bv is the current candidate. deps is the union of all
  // DominatingDepends of nodes in candidate.bv, minus things that are already
  // in candidate.bv.
  Candidate candidate;
  SparseBitVector<> deps;
  addNode(candidate, i);
  for (auto j : OutDep.ForcedDepends[i])
    addNode(candidate, j);
  for (auto j : candidate.bv)
    deps |= OutDep.DominatingDepends[j];
  deps.intersectWithComplement(candidate.bv);

  while (candidate.bv.count() <= options.max_nodes &&
         !OutDep.PreventsOutlining.intersects(candidate.bv)) {
    emitCandidate(candidate);

    // Generate the next candidate.
    // TODO: skip over:
    // - Candidates where dom is a block header, but we don't include any
    //   control flow that leads to it.
    // - Candidates where there is a single terminator consisting of an
    //   unconditional branch, not part of a loop.
    // - Other boring candidates.

    // Find the next node to add.
    int dom = candidate.bv.find_first();
    assert(dom >= 0);
    int next_dep = deps.find_last();
    if (next_dep < 0)
      break; // There are no more candidates.
    assert(next_dep < dom);
    if (!OutDep.Dominators[dom].test(next_dep))
      report_fatal_error("is this possible?");
    int new_op = next_dep;

    bool redundant = false;
    do {
      // Add the node and its dependences.
      assert(addNode(candidate, next_dep));
      deps |= OutDep.DominatingDepends[next_dep];
      for (auto j : OutDep.ForcedDepends[next_dep]) {
        if (j == i) {
          // This candidate, and all further candidates ending at node i, have
          // already been generated by generateCandidatesEndingAt(next_dep).
          // TODO: does this detect all duplicates, or only a subset?
          redundant = true;
          break;
        }
        if (addNode(candidate, j))
          deps |= OutDep.DominatingDepends[j];
      }
      if (redundant)
        break;

      // Forced depends may cause the new_op to move before next_dep, in which
      // case we need to add any dominating depends that lie between new_op and
      // next_dep. These depends, in turn, may have their own forced depends,
      // causing new_op to move again. We need to keep looping until we've
      // handled all dependences up to new_op.
      new_op = candidate.bv.find_first();
      deps.intersectWithComplement(candidate.bv);
      next_dep = deps.find_last();
    } while (next_dep >= new_op);

    if (redundant)
      break;
  }

  // Generate contiguous sequences ending at node i. We do this in case there
  // are sequences of related instructions (for example, initializing different
  // fields of a struct) that are not linked by dependences, so no candidates
  // will have been generated above.
  //
  // TODO: can we be smarter about this? For instance, we could only use
  // instructions that share at least one operand.
  Candidate contig_candidate;
  // Stop after OutlineMaxAdjacent nodes, or at the beginning of the block.
  int first_contig =
      static_cast<int>(i) + 1 - static_cast<int>(options.max_adjacent);
  first_contig = std::max(
      first_contig,
      static_cast<int>(OutDep.NodeIndices[cast<Instruction>(OutDep.Nodes[i])
                                              ->getParent()
                                              ->getFirstNonPHI()]));
  bool already_in_main_candidate = true;
  for (int j = i; j >= first_contig; j--) {
    addNode(contig_candidate, j);
    if (OutDep.PreventsOutlining.test(j))
      break;
    if (!contig_candidate.bv.contains(OutDep.ForcedDepends[j]))
      break;
    if (already_in_main_candidate && candidate.bv.test(j))
      continue;
    already_in_main_candidate = false;
    emitCandidate(contig_candidate);
  }
}

void OutliningCandidates::emitCandidate(Candidate &candidate) {
  if (!OutDep.isOutlinable(candidate.bv)) {
    OutDep.printSet(errs(), candidate.bv);
    errs() << "\n";
    report_fatal_error("invalid outlining candidate");
  }

  if (size_model) {
    // TODO: calculate this stuff incrementally (store intermediate results in
    // the Candidate).

    int candidate_size = 0;
    for (auto i : candidate.bv)
      if (auto ins = dyn_cast<Instruction>(OutDep.Nodes[i]))
        candidate_size += size_model->instruction_sizes.lookup(ins);

    // For each modified caller, we can delete the outlined instructions, but
    // we need to add a new call instruction.
    candidate.caller_savings =
        candidate_size - size_model->call_instruction_size;

    // For each new callee, we need to create a new function and fill it with
    // instructions.
    candidate.callee_size = size_model->estimateSize(
        candidate_size, candidate.bv.intersects(OutDep.CompilesToCall));

    if (candidate.caller_savings < options.min_caller_savings)
      return;

    OutliningCalleeExtractor extractor(F, OutDep, candidate.bv);
    if (extractor.getNumArgs() + extractor.getNumReturnValues() >
        options.max_args)
      return;
    candidate.arg_types.clear();
    candidate.result_types.clear();
    extractor.getArgTypes(candidate.arg_types);
    extractor.getResultTypes(candidate.result_types);
  }

  Candidates.emplace_back(candidate);
}

bool OutliningCandidates::addNode(Candidate &candidate, size_t i) {
  bool changed = candidate.bv.test_and_set(i);
  if (!changed)
    return false;
  for (GlobalValue *gv : OutDep.getGlobalsUsed()[i])
    candidate.globals_used.insert(gv);
  return true;
}

void OutliningCandidates::print(raw_ostream &OS) const {
  for (const Candidate &candidate : Candidates) {
    OS << "candidate: [";
    OutDep.printSet(OS, candidate.bv);
    OS << "], caller_savings " << candidate.caller_savings << ", callee_size "
       << candidate.callee_size << ", type [";
    for (Type *type : candidate.arg_types)
      OS << ' ' << *type;
    OS << " ] => [";
    for (Type *type : candidate.result_types)
      OS << ' ' << *type;
    OS << " ]\n";
  }
}

AnalysisKey OutliningCandidatesAnalysis::Key;

OutliningCandidatesAnalysis::OutliningCandidatesAnalysis(
    const OutliningCandidatesOptions &options)
    : options(options) {}

OutliningCandidates
OutliningCandidatesAnalysis::run(Function &f, FunctionAnalysisManager &am) {
  const SizeModelResults *size_model = nullptr;
  auto &deps = am.getResult<OutliningDependenceAnalysis>(f);
  if (!OutlineUnprofitable && OutlineOnly.empty())
    size_model = &am.getResult<SizeModelAnalysis>(f);
  return OutliningCandidates(f, deps, size_model, options);
}

PreservedAnalyses
OutliningCandidatesPrinterPass::run(Function &f, FunctionAnalysisManager &am) {
  auto &candidates = am.getResult<OutliningCandidatesAnalysis>(f);
  os << "OutliningCandidates for function: " << f.getName() << "\n";
  candidates.print(os);
  return PreservedAnalyses::all();
}

OutliningCandidatesWrapperPass::OutliningCandidatesWrapperPass()
    : FunctionPass(ID) {}

bool OutliningCandidatesWrapperPass::runOnFunction(Function &F) {
  auto &OutDep = getAnalysis<OutliningDependenceWrapperPass>().getOutDep();
  const SizeModelResults *size_model = nullptr;
  if (!OutlineUnprofitable && OutlineOnly.empty())
    size_model = &getAnalysis<SizeModelWrapperPass>().getSizeModel();
  OutCands.emplace(F, OutDep, size_model,
                   OutliningCandidatesOptions::getFromCommandLine());
  return false;
}

void OutliningCandidatesWrapperPass::print(raw_ostream &OS,
                                           const Module *M) const {
  OutCands->print(OS);
}

void OutliningCandidatesWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<OutliningDependenceWrapperPass>();
  if (!OutlineUnprofitable && OutlineOnly.empty())
    AU.addRequiredTransitive<SizeModelWrapperPass>();
}

void OutliningCandidatesWrapperPass::releaseMemory() { OutCands.reset(); }

void OutliningCandidatesWrapperPass::verifyAnalysis() const {
  assert(false && "unimplemented");
}

char OutliningCandidatesWrapperPass::ID = 0;
namespace {
struct RegisterPassX : RegisterPass<OutliningCandidatesWrapperPass> {
  RegisterPassX()
      : RegisterPass("outlining-candidates",
                     "Outlining Candidates Analysis Pass", false, true) {
    // Ensure required passes are loaded, even if this pass used in a program
    // that doesn't load all the standard LLVM passes.
    initializeDominatorTreeWrapperPassPass(*PassRegistry::getPassRegistry());
    initializePostDominatorTreeWrapperPassPass(
        *PassRegistry::getPassRegistry());
    initializeMemorySSAWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};
} // end anonymous namespace
static RegisterPassX X;
