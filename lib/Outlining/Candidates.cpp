#include "Candidates.h"

#include <llvm/IR/InstrTypes.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FormatVariadic.h>

#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

static cl::list<int> OutlineOnly("outline-only", cl::CommaSeparated,
                                 cl::ZeroOrMore,
                                 cl::desc("Specify nodes to outline"),
                                 cl::value_desc("node,node,..."));

static cl::opt<size_t>
    OutlineMaxAdjacent("outline-max-adjacent", cl::init(10),
                       cl::desc("Maximum number of unrelated adjacent "
                                "instructions to outline together."));

static cl::opt<bool> OutlineUnprofitable(
    "outline-unprofitable",
    cl::desc(
        "Outline every possible sequence, even if it seems unprofitable."));

OutliningCandidates::OutliningCandidates(Function &F,
                                         OutliningDependenceResults &OutDep)
    : F(F), OutDep(OutDep) {
  if (!OutlineOnly.empty()) {
    BitVector BV(OutDep.Nodes.size());
    for (int i : OutlineOnly)
      BV.set(i);
    if (!OutDep.isOutlinable(BV))
      report_fatal_error("Specified nodes cannot be outlined",
                         /* gen_crash_diag */ false);
    Candidates.push_back(std::move(BV));
    return;
  }

  createInitialCandidates();
  while (!Queue.empty()) {
    BitVector BV = std::move(Queue.back());
    Queue.pop_back();
    processCandidate(std::move(BV));
  }
}

void OutliningCandidates::print(raw_ostream &OS) const {
  for (const BitVector &BV : Candidates)
    OS << formatv("candidate: [{0}]\n", BV.set_bits());
}

void OutliningCandidates::createInitialCandidates() {
  // Use all possible contiguous regions of each basic block.
  for (BasicBlock &BB : F) {
    if (!OutDep.NodeIndices.count(&BB))
      continue;
    size_t First = OutDep.NodeIndices[BB.getFirstNonPHI()];
    size_t Last = OutDep.NodeIndices[BB.getTerminator()];

    // Don't bother outlining unconditional branches.
    if (BB.getTerminator()->getOpcode() == Instruction::Br &&
        BB.getSingleSuccessor())
      Last--;

    for (; First <= Last; First++) {
      BitVector BV(OutDep.Nodes.size());
      for (size_t i = First; i <= Last && i - First < OutlineMaxAdjacent; i++) {
        BV.set(i);
        BV |= OutDep.ForcedDepends[i];
        queueBV(BV);
      }
    }
  }
}

void OutliningCandidates::queueBV(BitVector BV) {
  auto Pair = AlreadyVisited.insert(std::move(BV));
  if (Pair.second) {
    Queue.push_back(*Pair.first);
  }
}

void OutliningCandidates::processCandidate(BitVector BV) {
  if (OutDep.PreventsOutlining.anyCommon(BV))
    return;

  BitVector ArgInputs, ExternalInputs, ExternalOutputs;
  OutDep.getExternals(BV, ArgInputs, ExternalInputs, ExternalOutputs);
  int score =
      -1 - ArgInputs.count() - ExternalInputs.count() - ExternalOutputs.count();
  for (size_t i : BV.set_bits()) {
    if (Instruction *I = dyn_cast<Instruction>(OutDep.Nodes[i])) {
      score += 1;
      if (CallBase *CB = dyn_cast<CallBase>(I))
        score += CB->arg_size();
    }
  }
  assert(OutDep.isOutlinable(BV));
  if (OutlineUnprofitable || score > 0)
    Candidates.push_back(BV);

  size_t DomI = BV.find_first();

  // TODO: exclude:
  // - candidates where DomI is a block header, but we don't include any control
  //   flow that leads to it
  // - candidates where there is a single terminator consisting of an
  //   unconditional branch
  // - in general, boring terminators need more thought

  // Generate the next candidate.
  BitVector Deps;
  for (size_t i : BV.set_bits())
    Deps |= OutDep.DominatingDepends[i];
  int NewI = Deps.find_prev(DomI);
  if (NewI < 0)
    return;
  BV.set(NewI);
  BV |= OutDep.ForcedDepends[NewI];
  queueBV(BV);
}

OutliningCandidatesWrapperPass::OutliningCandidatesWrapperPass()
    : FunctionPass(ID) {}

bool OutliningCandidatesWrapperPass::runOnFunction(Function &F) {
  auto &OutDep = getAnalysis<OutliningDependenceWrapperPass>().getOutDep();
  OutCands.emplace(F, OutDep);
  return false;
}

void OutliningCandidatesWrapperPass::print(raw_ostream &OS,
                                           const Module *M) const {
  OutCands->print(OS);
}

void OutliningCandidatesWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<OutliningDependenceWrapperPass>();
}

void OutliningCandidatesWrapperPass::releaseMemory() { OutCands.reset(); }

void OutliningCandidatesWrapperPass::verifyAnalysis() const {
  assert(false && "unimplemented");
}

char OutliningCandidatesWrapperPass::ID = 0;
static RegisterPass<OutliningCandidatesWrapperPass>
    X("outlining-candidates", "Outlining Candidates Analysis Pass", false,
      true);