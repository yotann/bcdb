#include "Extractor.h"

#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include "Candidates.h"

using namespace bcdb;
using namespace llvm;

OutliningExtractor::OutliningExtractor(Function &F,
                                       OutliningDependenceResults &OutDep,
                                       BitVector &BV)
    : F(F), OutDep(OutDep), BV(BV) {
  auto &PDT = OutDep.PDT;
  auto &Nodes = OutDep.Nodes;
  auto &NodeIndices = OutDep.NodeIndices;

  BitVector ArgInputs, ExternalInputs, ExternalOutputs;
  OutDep.getExternals(BV, ArgInputs, ExternalInputs, ExternalOutputs);

  bool OutliningReturn = false;
  BitVector OutlinedBlocks(Nodes.size());
  for (size_t i : BV.set_bits()) {
    if (isa<BasicBlock>(Nodes[i]))
      OutlinedBlocks.set(i);
    else if (Instruction *I = dyn_cast<Instruction>(Nodes[i])) {
      OutlinedBlocks.set(NodeIndices[I->getParent()]);
      if (isa<ReturnInst>(I) && !F.getReturnType()->isVoidTy())
        OutliningReturn = true;
    }
  }

  // Determine the type of the outlined function.
  SmallVector<Type *, 8> Types;
  if (OutliningReturn)
    Types.push_back(F.getReturnType());
  for (size_t i : ExternalOutputs.set_bits())
    Types.push_back(Nodes[i]->getType());
  Type *ResultType = StructType::get(F.getContext(), Types);
  Types.clear();
  for (size_t i : ArgInputs.set_bits())
    Types.push_back((F.arg_begin() + i)->getType());
  for (size_t i : ExternalInputs.set_bits())
    Types.push_back(Nodes[i]->getType());
  FunctionType *FuncType =
      FunctionType::get(ResultType, Types, /* isVarArg */ false);

  // Create the new function.
  std::string NewName;
  raw_string_ostream NewNameOS(NewName);
  NewNameOS << F.getName() << ".outlined";
  for (int Start = BV.find_first(); Start >= 0; Start = BV.find_next(Start)) {
    int End = BV.find_next_unset(Start);
    if (End < 0)
      End = BV.size();
    if (Start + 1 == End)
      NewNameOS << formatv(".{0}", Start);
    else
      NewNameOS << formatv(".{0}-{1}", Start, End - 1);
    Start = End - 1;
  }
  NewF = Function::Create(FuncType, GlobalValue::ExternalLinkage,
                          NewNameOS.str(), F.getParent());
  if (F.hasPersonalityFn())
    NewF->setPersonalityFn(F.getPersonalityFn());

  // Add entry and exit blocks. We need a new entry block because we might be
  // outlining a loop, and LLVM prohibits the entry block from being part of a
  // loop. We need a new exit block so we can set up the return value.
  BasicBlock *EntryBlock =
      BasicBlock::Create(NewF->getContext(), "outline_entry", NewF);
  BasicBlock *ExitBlock =
      BasicBlock::Create(NewF->getContext(), "outline_return", NewF);
  PHINode *RetValuePhi = nullptr;
  if (OutliningReturn)
    RetValuePhi = PHINode::Create(F.getReturnType(), 0, "", ExitBlock);

  // Set up the return instruction and PHI nodes.
  DenseMap<size_t, PHINode *> OutputPhis;
  for (size_t i : ExternalOutputs.set_bits())
    OutputPhis[i] = PHINode::Create(Nodes[i]->getType(), 0, "", ExitBlock);
  Value *ResultValue = UndefValue::get(ResultType);
  unsigned ResultI = 0;
  if (OutliningReturn)
    ResultValue = InsertValueInst::Create(ResultValue, RetValuePhi, {ResultI++},
                                          "", ExitBlock);
  for (size_t i : ExternalOutputs.set_bits())
    ResultValue = InsertValueInst::Create(ResultValue, OutputPhis[i],
                                          {ResultI++}, "", ExitBlock);
  ReturnInst::Create(NewF->getContext(), ResultValue, ExitBlock);

  // Create the value map and fill in the input values.
  ValueToValueMapTy VMap;
  ValueMapper VM(VMap, RF_NoModuleLevelChanges);
  Function::arg_iterator ArgI = NewF->arg_begin();
  for (size_t i : ArgInputs.set_bits()) {
    Argument &Src = *(F.arg_begin() + i);
    Argument &Dst = *ArgI++;
    VMap[&Src] = &Dst;
    Dst.setName(Src.getName());
  }
  for (size_t i : ExternalInputs.set_bits()) {
    Value &Src = *Nodes[i];
    Argument &Dst = *ArgI++;
    VMap[&Src] = &Dst;
    Dst.setName(Src.getName());
  }
  assert(ArgI == NewF->arg_end());

  // Map blocks in the original function to blocks in the outlined function.
  DenseMap<BasicBlock *, BasicBlock *> BBMap;
  auto mapBlock = [&](BasicBlock *BB) {
    if (VMap[BB])
      return;
    // We may be outlining a branch (either a conditional branch, or an
    // implicit unconditional branch) that goes to block BB even though we
    // aren't outlining any instructions in BB. In that case, use the
    // postdominator tree to skip blocks until we find one that actually is
    // being outlined.
    BasicBlock *PDom = BB;
    while (!OutlinedBlocks.test(NodeIndices[PDom])) {
      PDom = PDT[PDom]->getIDom()->getBlock();
      if (!PDom) {
        VMap[BB] = ExitBlock;
        return;
      }
    }
    if (!BBMap.count(PDom))
      BBMap[PDom] =
          BasicBlock::Create(NewF->getContext(), PDom->getName(), NewF);
    VMap[BB] = BBMap[PDom];
  };
  for (BasicBlock &BB : F)
    if (NodeIndices.count(&BB))
      mapBlock(&BB);

  // Jump from the entry block to the first actual outlined block.
  BasicBlock *FirstBlock = cast<BasicBlock>(Nodes[OutlinedBlocks.find_first()]);
  BranchInst::Create(BBMap[FirstBlock], EntryBlock);

  // Clone the selected instructions into the outlined function. Also remap
  // their arguments.
  SmallVector<PHINode *, 16> PHIToResolve;
  for (size_t i : BV.set_bits()) {
    if (Instruction *I = dyn_cast<Instruction>(Nodes[i])) {
      BasicBlock *BB = BBMap[I->getParent()];
      Instruction *NewI;
      if (isa<ReturnInst>(I)) {
        // Return instructions become branches to the new exit block.
        if (OutliningReturn)
          RetValuePhi->addIncoming(VM.mapValue(*I->getOperand(0)), BB);
        NewI = BranchInst::Create(ExitBlock);
      } else {
        NewI = I->clone();
        // PHI nodes can't be remapped until the other instructions are done.
        if (PHINode *PN = dyn_cast<PHINode>(NewI))
          PHIToResolve.push_back(PN);
        else
          VM.remapInstruction(*NewI);
      }
      VMap[I] = NewI;
      NewI->setName(I->getName());
      BB->getInstList().push_back(NewI);
    }
  }

  // Remap PHI nodes.
  for (PHINode *PN : PHIToResolve) {
    // If the original function had unreachable blocks, we may need to remove
    // them from PHI nodes.
    for (int i = PN->getNumIncomingValues() - 1; i >= 0; i--) {
      BasicBlock *Pred = PN->getIncomingBlock(i);
      if (!OutDep.DT[Pred] || !OutDep.PDT[Pred])
        PN->removeIncomingValue(Pred);
    }

    VM.remapInstruction(*PN);
  }

  // Add terminators to blocks that didn't have their terminator selected for
  // outlining.
  for (auto &Item : BBMap) {
    BasicBlock *BB = Item.first;
    BasicBlock *NewBB = Item.second;
    if (!NewBB->getTerminator()) {
      auto PDom = PDT[BB]->getIDom();
      BasicBlock *Target = ExitBlock;
      if (PDom && PDom->getBlock())
        Target = cast<BasicBlock>(VMap[PDom->getBlock()]);
      BranchInst::Create(Target, NewBB);
    }
  }

  // Fill in the PHI nodes in the exit block.
  for (auto &Item : BBMap) {
    BasicBlock *BB = Item.first;
    BasicBlock *NewBB = Item.second;

    // We only need to do this if NewBB branches to the exit block at least
    // once. NewBB could branch to the exit block multiple times! (E.g.,
    // multiple cases of a switch instruction).
    int NumBranchesToExit = 0;
    for (BasicBlock *Succ : successors(NewBB))
      if (Succ == ExitBlock)
        NumBranchesToExit++;
    if (!NumBranchesToExit)
      continue;

    if (RetValuePhi && RetValuePhi->getBasicBlockIndex(NewBB) < 0)
      for (int j = 0; j < NumBranchesToExit; j++)
        RetValuePhi->addIncoming(UndefValue::get(F.getReturnType()), NewBB);
    for (size_t i : ExternalOutputs.set_bits()) {
      Value *V = UndefValue::get(Nodes[i]->getType());
      if (OutDep.DT.dominates(cast<Instruction>(Nodes[i]), BB->getTerminator()))
        V = VMap[Nodes[i]];
      for (int j = 0; j < NumBranchesToExit; j++)
        OutputPhis[i]->addIncoming(V, NewBB);
    }
  }

  // If we're outlining an infinite loop, we shouldn't have an exit block.
  if (pred_empty(ExitBlock))
    ExitBlock->eraseFromParent();

  // TODO: calculate outlining cost
  // - benefit of outlining the instructions (per-caller)
  // - cost of setting up arguments (per-caller)
  // - cost of the call instruction (per-caller)
  // - cost of returning values (once)
  // - cost of the outlined function body (once)
  // - see getInlineCost()
  //   - most instructions cost InstrCost
  //   - bitcasts, unconditional branches, first return instruction,
  //     unreachable are all zero cost
  //   - getelementptr, ptrtoint, inttoptr, cast use
  //     TargetTransformInfo::getUserCost()
  //   - call cost is arg_size()*InstrCost + CallPenalty
}

OutliningExtractorWrapperPass::OutliningExtractorWrapperPass()
    : ModulePass(ID) {}

bool OutliningExtractorWrapperPass::runOnModule(Module &M) {
  // Only run on functions that already existed when we began the pass.
  SmallVector<Function *, 1> Functions;
  for (Function &F : M)
    if (!F.isDeclaration())
      Functions.push_back(&F);
  for (Function *F : Functions) {
    auto NF = runOnFunction(*F);
    if (!NF.empty())
      NewFunctions[F] = std::move(NF);
  }
  return !NewFunctions.empty();
}

std::vector<std::pair<BitVector, Function *>>
OutliningExtractorWrapperPass::runOnFunction(Function &F) {
  std::vector<std::pair<BitVector, Function *>> Results;
  OutliningDependenceResults &OutDep =
      getAnalysis<OutliningDependenceWrapperPass>(F).getOutDep();
  auto &OutCands = getAnalysis<OutliningCandidatesWrapperPass>(F).getOutCands();
  for (BitVector &BV : OutCands.Candidates) {
    OutliningExtractor Extractor(F, OutDep, BV);
    if (Extractor.NewF)
      Results.emplace_back(BV, Extractor.NewF);
  }
  return Results;
}

void OutliningExtractorWrapperPass::print(raw_ostream &OS,
                                          const Module *M) const {}

void OutliningExtractorWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<OutliningDependenceWrapperPass>();
  AU.addRequired<OutliningCandidatesWrapperPass>();
}

char OutliningExtractorWrapperPass::ID = 0;
static RegisterPass<OutliningExtractorWrapperPass>
    X("outlining-extractor", "Outlining Extractor Pass", false, false);
