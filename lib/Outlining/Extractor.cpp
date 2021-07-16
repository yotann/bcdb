#include "Outlining/Extractor.h"

#include <llvm/ADT/SparseBitVector.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include "Outlining/Candidates.h"

using namespace bcdb;
using namespace llvm;

OutliningExtractor::OutliningExtractor(Function &F,
                                       OutliningDependenceResults &OutDep,
                                       SparseBitVector<> &BV)
    : F(F), OutDep(OutDep), BV(BV) {
  auto &Nodes = OutDep.Nodes;
  auto &NodeIndices = OutDep.NodeIndices;

  if (!OutDep.isOutlinable(BV))
    report_fatal_error("Specified nodes cannot be outlined",
                       /* gen_crash_diag */ false);

  auto addInputValue = [&](Value *v) {
    if (Argument *arg = dyn_cast<Argument>(v))
      ArgInputs.set(arg->getArgNo());
    else if (NodeIndices.count(v))
      ExternalInputs.set(NodeIndices[v]);
    else
      assert(isa<Constant>(v) && "impossible");
  };

  // Determine which nodes inside the new callee will need to have their
  // results passed back to the new caller.
  for (size_t i = 0; i < Nodes.size(); i++) {
    if (BV.test(i))
      continue;
    if (PHINode *phi = dyn_cast<PHINode>(Nodes[i])) {
      SmallPtrSet<Value *, 8> phi_incoming;
      for (unsigned j = 0; j < phi->getNumIncomingValues(); j++) {
        Value *v = phi->getIncomingValue(j);
        if (BV.test(NodeIndices[phi->getIncomingBlock(j)->getTerminator()])) {
          // May depend on control flow in the callee.
          phi_incoming.insert(v);
        } else if (NodeIndices.count(v)) {
          // Only depends on control flow from the caller, but may need a value
          // from the callee.
          ExternalOutputs.set(NodeIndices[v]);
        }
      }
      if (phi_incoming.size() > 1) {
        // The phi value partly depends on control flow within the outlined
        // callee, so we need to outline part or all of the phi. We also need
        // to make sure the appropriate phi input values are accessible. We do
        // not need external outputs for the individual phi values.
        for (Value *v : phi_incoming)
          addInputValue(v);
        output_phis.set(i);
      } else if (!phi_incoming.empty()) {
        // Only one phi value is used for all paths within the callee, so we
        // return that value directly and use it in the phi in the caller.
        ExternalOutputs.set(NodeIndices[*phi_incoming.begin()]);
      }
    } else {
      ExternalOutputs |= OutDep.DataDepends[i];
    }
  }
  ExternalOutputs &= BV;
  ExternalOutputs |= output_phis;

  // Determine which function arguments and other nodes need to be passed as
  // arguments to the new callee.
  for (auto i : BV) {
    if (PHINode *phi = dyn_cast<PHINode>(Nodes[i])) {
      SmallPtrSet<Value *, 8> phi_incoming;
      for (unsigned j = 0; j < phi->getNumIncomingValues(); j++) {
        if (BV.test(NodeIndices[phi->getIncomingBlock(j)->getTerminator()])) {
          // This part of the phi will be outlined, so we need to make sure the
          // appropriate phi input values are accessible.
          addInputValue(phi->getIncomingValue(j));
        } else {
          phi_incoming.insert(phi->getIncomingValue(j));
        }
      }
      if (phi_incoming.size() > 1) {
        // The phi value partly depends on control flow within the outlined
        // caller, so we need to leave part or all of the phi in the caller,
        // and provide its value to the callee.
        input_phis.set(i);
      } else if (!phi_incoming.empty()) {
        // The phi value only depends on control flow within the outlined
        // callee, but there's one value we need from the caller.
        addInputValue(*phi_incoming.begin());
      }
    } else {
      ExternalInputs |= OutDep.DataDepends[i];
      ArgInputs |= OutDep.ArgDepends[i];
    }
  }
  // The next statement must be executed after all calls to addInputValue().
  ExternalInputs.intersectWithComplement(BV);
  ExternalInputs |= input_phis;

  for (auto i : BV) {
    if (isa<BasicBlock>(Nodes[i]))
      OutlinedBlocks.set(i);
    else if (Instruction *I = dyn_cast<Instruction>(Nodes[i]))
      OutlinedBlocks.set(NodeIndices[I->getParent()]);
  }
}

unsigned OutliningExtractor::getNumCalleeArgs() const {
  return ArgInputs.count() + ExternalInputs.count();
}

unsigned OutliningExtractor::getNumCalleeReturnValues() const {
  return ExternalOutputs.count();
}

void OutliningExtractor::getArgTypes(SmallVectorImpl<Type *> &types) const {
  for (auto i : ArgInputs)
    types.push_back((F.arg_begin() + i)->getType());
  for (auto i : ExternalInputs)
    types.push_back(OutDep.Nodes[i]->getType());
}

void OutliningExtractor::getResultTypes(SmallVectorImpl<Type *> &types) const {
  for (auto i : ExternalOutputs)
    types.push_back(OutDep.Nodes[i]->getType());
}

Function *OutliningExtractor::createNewCallee() {
  if (NewCallee)
    return NewCallee;

  auto &PDT = OutDep.PDT;
  auto &Nodes = OutDep.Nodes;
  auto &NodeIndices = OutDep.NodeIndices;

  // Determine the type of the outlined function.
  // FIXME: Canonicalize arguments and return values by reordering them. Also
  // make all pointers opaque.
  SmallVector<Type *, 8> result_types, arg_types;
  for (auto i : ExternalOutputs)
    result_types.push_back(Nodes[i]->getType());
  Type *result_type = StructType::get(F.getContext(), result_types);
  for (auto i : ArgInputs)
    arg_types.push_back((F.arg_begin() + i)->getType());
  for (auto i : ExternalInputs)
    arg_types.push_back(Nodes[i]->getType());
  CalleeType = FunctionType::get(result_type, arg_types, /* isVarArg */ false);

  raw_string_ostream NewNameOS(NewName);
  NewNameOS << F.getName() << ".outlined.";
  // Use assembler-friendly characters "." and "_".
  OutDep.printSet(NewNameOS, BV, ".", "_");
  NewCallee = Function::Create(CalleeType, GlobalValue::ExternalLinkage,
                               NewNameOS.str() + ".callee", F.getParent());

  // Pass more arguments and return values in registers.
  // TODO: experiment to check whether the code is actually smaller this way.
  NewCallee->setCallingConv(CallingConv::Fast);

  // Add function attributes.
  //
  // Note that we don't add the uwtable attribute. If the function never throws
  // exceptions, that means the unwinding table will be omitted, making the
  // function smaller but also preventing backtraces from working correctly.
  NewCallee->addFnAttr(Attribute::MinSize);
  NewCallee->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  if (F.hasPersonalityFn()) {
    for (auto i : BV) {
      if (Instruction *ins = dyn_cast<Instruction>(Nodes[i])) {
        if (ins->isEHPad()) {
          NewCallee->setPersonalityFn(F.getPersonalityFn());
          break;
        }
      }
    }
  }

  // Add entry and exit blocks. We need a new entry block because we might be
  // outlining a loop, and LLVM prohibits the entry block from being part of a
  // loop. We need a new exit block so we can set up the return value.
  BasicBlock *EntryBlock =
      BasicBlock::Create(NewCallee->getContext(), "outline_entry", NewCallee);
  BasicBlock *ExitBlock =
      BasicBlock::Create(NewCallee->getContext(), "outline_return", NewCallee);

  // Create the value map and fill in the input values.
  ValueToValueMapTy VMap;
  // We need a separate map for phis in the input block that are being split,
  // because we don't want to rewrite uses within the callee to use the value
  // passed in as an argument.
  ValueToValueMapTy input_phi_map;
  // We need RF_IgnoreMissingLocals because we may add new, already-remapped
  // values and blocks to PHI nodes before calling remapInstruction on them.
  ValueMapper VM(VMap, RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);

  // Map the callee's arguments to input values.
  Function::arg_iterator new_arg_iter = NewCallee->arg_begin();
  for (auto i : ArgInputs) {
    Argument &orig_arg = *(F.arg_begin() + i);
    Argument &new_arg = *new_arg_iter++;
    VMap[&orig_arg] = &new_arg;
    new_arg.setName(orig_arg.getName());
  }
  for (auto i : ExternalInputs) {
    Value &orig_value = *Nodes[i];
    Argument &new_value = *new_arg_iter++;
    if (input_phis.test(i))
      input_phi_map[&orig_value] = &new_value;
    else
      VMap[&orig_value] = &new_value;
    new_value.setName(orig_value.getName());
  }
  assert(new_arg_iter == NewCallee->arg_end());

  // Map blocks in the original function to blocks in the outlined function.
  DenseMap<BasicBlock *, BasicBlock *> BBMap;
  for (BasicBlock &BB : F) {
    if (!NodeIndices.count(&BB))
      continue; // unreachable block
    // We may be outlining a branch (either a conditional branch, or an
    // implicit unconditional branch) that goes to block BB even though we
    // aren't outlining any instructions in BB. In that case, use the
    // postdominator tree to skip blocks until we find one that actually is
    // being outlined.
    BasicBlock *PDom = &BB;
    while (PDom && !OutlinedBlocks.test(NodeIndices[PDom]))
      PDom = PDT[PDom]->getIDom()->getBlock();
    if (!PDom) {
      VMap[&BB] = ExitBlock;
      continue;
    }
    if (!BBMap.count(PDom))
      BBMap[PDom] = BasicBlock::Create(NewCallee->getContext(), PDom->getName(),
                                       NewCallee);
    VMap[&BB] = BBMap[PDom];
  }

  // Jump from the entry block to the first actual outlined block.
  BasicBlock *FirstBlock = cast<BasicBlock>(Nodes[OutlinedBlocks.find_first()]);
  BranchInst::Create(BBMap[FirstBlock], EntryBlock);

  // Clone the selected instructions into the outlined function.
  for (auto i : BV) {
    if (Instruction *orig_ins = dyn_cast<Instruction>(Nodes[i])) {
      BasicBlock *new_block = BBMap[orig_ins->getParent()];
      Instruction *new_ins = orig_ins->clone();
      VMap[orig_ins] = new_ins;
      new_ins->setName(orig_ins->getName());
      new_block->getInstList().push_back(new_ins);
    }
  }

  // Remap instruction operands.
  for (auto i : BV) {
    Instruction *orig_ins = dyn_cast<Instruction>(Nodes[i]);
    if (!orig_ins)
      continue;
    Instruction *new_ins = cast<Instruction>(VMap[orig_ins]);

    if (PHINode *orig_phi = dyn_cast<PHINode>(orig_ins)) {
      PHINode *new_phi = cast<PHINode>(new_ins);
      SmallPtrSet<Value *, 8> phi_incoming;

      // Iterate backwards so we don't have to care about removeIncomingValue
      // renumbering things.
      for (unsigned j = orig_phi->getNumIncomingValues(); j > 0; --j) {
        BasicBlock *orig_pred = orig_phi->getIncomingBlock(j - 1);
        if (!NodeIndices.count(orig_pred)) {
          // unreachable block
          new_phi->removeIncomingValue(j - 1, /*DeletePHIIfEmpty*/ false);
          continue;
        }
        if (!BV.test(NodeIndices[orig_pred->getTerminator()])) {
          // This incoming block is not being outlined. Instead, we will have
          // EntryBlock as a predecessor.
          phi_incoming.insert(orig_phi->getIncomingValue(j - 1));
          new_phi->removeIncomingValue(j - 1, /*DeletePHIIfEmpty*/ false);
        }
      }

      if (phi_incoming.empty()) {
        // nothing to do
      } else if (input_phis.test(i)) {
        // Some values depend on control flow in the caller, so the caller must
        // calculate the phi value in those cases and pass it to us.
        new_phi->addIncoming(input_phi_map[orig_phi], EntryBlock);
      } else {
        // The value is always the same when we first come in from the caller,
        // so we use that value.
        assert(phi_incoming.size() == 1);
        // Will be remapped to the new value by remapInstruction() below.
        new_phi->addIncoming(*phi_incoming.begin(), EntryBlock);
      }
    }

    VM.remapInstruction(*new_ins);
  }

  // Add terminators to blocks that didn't have their terminator selected for
  // outlining.
  for (auto &Item : BBMap) {
    BasicBlock *orig_block = Item.first;
    BasicBlock *new_block = Item.second;
    if (!new_block->getTerminator()) {
      auto PDom = PDT[orig_block]->getIDom();
      BasicBlock *new_target = ExitBlock;
      if (PDom && PDom->getBlock())
        new_target = cast<BasicBlock>(VMap[PDom->getBlock()]);
      BranchInst::Create(new_target, new_block);
    }
  }

  // Set up PHI nodes that were not chosen for outlining, but which depend on
  // control flow in the outlined set.
  for (auto i : output_phis) {
    PHINode *orig_phi = cast<PHINode>(Nodes[i]);
    PHINode *new_phi =
        PHINode::Create(orig_phi->getType(), 0, orig_phi->getName(), ExitBlock);
    VMap[orig_phi] = new_phi;
    for (unsigned j = 0; j < orig_phi->getNumIncomingValues(); ++j) {
      BasicBlock *orig_pred = orig_phi->getIncomingBlock(j);
      if (!NodeIndices.count(orig_pred))
        continue; // unreachable block
      if (BV.test(NodeIndices[orig_pred->getTerminator()]))
        new_phi->addIncoming(orig_phi->getIncomingValue(j), orig_pred);
    }
    VM.remapInstruction(*new_phi);
  }

  // Set up the return instruction.
  Value *ResultValue = UndefValue::get(CalleeType->getReturnType());
  unsigned ResultI = 0;
  for (auto i : ExternalOutputs)
    ResultValue = InsertValueInst::Create(ResultValue, VMap[Nodes[i]],
                                          {ResultI++}, "", ExitBlock);
  ReturnInst::Create(NewCallee->getContext(), ResultValue, ExitBlock);

  // If we're outlining an infinite loop, we shouldn't have an exit block.
  if (pred_empty(ExitBlock))
    ExitBlock->eraseFromParent();

  return NewCallee;
}

Function *OutliningExtractor::createNewCaller() {
  createNewCallee();

  auto &PDT = OutDep.PDT;
  auto &Nodes = OutDep.Nodes;
  auto &NodeIndices = OutDep.NodeIndices;

  ValueToValueMapTy VMap;
  NewCaller = CloneFunction(&F, VMap, nullptr);
  NewCaller->setName(NewName + ".caller");

  // Identify outlining point.
  Value *OutliningPoint = Nodes[BV.find_first()];
  Instruction *InsertionPoint;
  if (isa<Instruction>(OutliningPoint)) {
    InsertionPoint = cast<Instruction>(VMap[OutliningPoint]);
  } else if (isa<BasicBlock>(OutliningPoint)) {
    InsertionPoint = cast<BasicBlock>(VMap[OutliningPoint])->getFirstNonPHI();
  } else {
    // MemoryPhi is impossible here because it has a forced dependence on the
    // corresponding BasicBlock.
    llvm_unreachable("Impossible node type");
  }

  // Construct the call.
  SmallVector<Value *, 8> Args;
  for (auto i : ArgInputs)
    Args.push_back(NewCaller->arg_begin() + i);
  for (auto i : ExternalInputs)
    Args.push_back(VMap[Nodes[i]]);
  CallInst *CI = CallInst::Create(NewCallee->getFunctionType(), NewCallee, Args,
                                  "", InsertionPoint);
  CI->setCallingConv(NewCallee->getCallingConv());

  // Extract outputs.
  DenseMap<size_t, Value *> OutputValues;
  unsigned ResultI = 0;
  for (auto i : ExternalOutputs)
    OutputValues[i] =
        ExtractValueInst::Create(CI, {ResultI++}, "", InsertionPoint);

  // Prepare to delete outlined instructions. We don't actually delete any
  // BasicBlocks, because that would complicate the handling of PHI nodes. Any
  // unreachable or empty blocks can be deleted later by the SimplifyCFG pass.
  SmallVector<Instruction *, 16> insns_to_delete;
  for (auto i : BV) {
    if (Instruction *orig_ins = dyn_cast<Instruction>(Nodes[i])) {
      if (isa<PHINode>(orig_ins) && orig_ins->getParent() == OutliningPoint) {
        // PHI node in the first block being outlined. We shouldn't necessarily
        // delete it; if it depends on control flow in the caller, we need to
        // pass the result of the PHI into the callee. We do need to remove the
        // PHI values that depend on control flow in the callee, because we're
        // removing the corresponding CFG edges.
        PHINode *orig_phi = cast<PHINode>(orig_ins);
        PHINode *new_phi = cast<PHINode>(VMap[orig_phi]);
        // Iterate backwards so we don't have to care about removeIncomingValue
        // renumbering things.
        for (unsigned j = orig_phi->getNumIncomingValues(); j > 0; --j)
          if (BV.test(NodeIndices[orig_phi->getIncomingBlock(j - 1)
                                      ->getTerminator()]))
            new_phi->removeIncomingValue(j - 1, /*DeletePHIIfEmpty*/ false);
        continue;
      }
      insns_to_delete.push_back(cast<Instruction>(VMap[orig_ins]));
      if (orig_ins->isTerminator()) {
        // Replace a (possibly conditional) branch or other terminator with an
        // unconditional branch, skipping over outlined conditional code.
        auto PDom = PDT[orig_ins->getParent()]->getIDom();
        if (PDom && PDom->getBlock()) {
          BranchInst::Create(cast<BasicBlock>(VMap[PDom->getBlock()]),
                             cast<BasicBlock>(VMap[orig_ins->getParent()]));
        } else {
          // There are a few ways this can happen:
          //
          // 1. The terminator is a RetInst or an "unwind to caller"
          // instruction.
          // 2. The terminator is an UnreachableInst.
          // 3. The terminator is a ResumeInst.
          // 4. The terminator is part of an infinite loop.
          // 5. The terminator is a conditional branch with at least one path
          //    to one of cases 1-5.
          //
          // Case 1 is prohibited by OutliningDependence. In case 5, all
          // instructions that can be executed after the branch will be
          // (directly or indirectly) control-dependent on it, and
          // OutliningDependence requires all such instructions be outlined
          // whenever the branch is outlined; this is only allowed if all paths
          // after the branch eventually lead to cases 2-4. And no path leading
          // to cases 2-4 will ever return normally from the outlined callee.
          // So regardless of which case we have, this instruction is
          // unreachable.
          new UnreachableInst(orig_ins->getContext(),
                              cast<BasicBlock>(VMap[orig_ins->getParent()]));
        }
      }
    }
  }

  // Handle PHI nodes in the caller that depend on control flow within the
  // callee. Other phi values will be rewritten by replaceAllUsesWith below.
  for (size_t i : output_phis) {
    PHINode *orig_phi = cast<PHINode>(Nodes[i]);
    PHINode *new_phi = cast<PHINode>(VMap[orig_phi]);
    for (unsigned j = 0; j < orig_phi->getNumIncomingValues(); ++j)
      if (BV.test(NodeIndices[orig_phi->getIncomingBlock(j)->getTerminator()]))
        new_phi->setIncomingValue(j, OutputValues[i]);
  }

  // Replace uses of outlined instructions.
  //
  // XXX: this loop must be executed *after* the PHI node handling above.
  //
  // XXX: replaceAllUsesWith changes the values stored in VMap! So we can't use
  // VMap after executing this loop.
  for (auto i : ExternalOutputs) {
    if (!BV.test(i)) {
      // PHI node in the block after the call; handled separately.
      continue;
    }
    OutputValues[i]->takeName(VMap[Nodes[i]]);
    VMap[Nodes[i]]->replaceAllUsesWith(OutputValues[i]);
  }

  // Actually delete outlined instructions.
  for (Instruction *ins : insns_to_delete) {
    ins->replaceAllUsesWith(UndefValue::get(ins->getType()));
    ins->eraseFromParent();
  }

  return NewCaller;
}

static bool runOnFunction(Function &F, OutliningDependenceResults &OutDep,
                          OutliningCandidates &OutCands) {
  // We track outlined functions in the output module by adding metadata nodes.
  // We can't add the metadata to the new functions because that would prevent
  // the BCDB from deduplicating them, and it seems silly to modify the
  // original function just to add one piece of metadata. So we use
  // module-level named metadata.
  bool Changed = false;
  SmallVector<Metadata *, 8> MDNodes;

  for (OutliningCandidates::Candidate &candidate : OutCands.Candidates) {
    OutliningExtractor Extractor(F, OutDep, candidate.bv);
    Function *NewCallee = Extractor.createNewCallee();
    if (NewCallee) {
      Changed = true;

      Constant *NewCaller = Extractor.createNewCaller();
      if (!NewCaller)
        NewCaller = ConstantPointerNull::get(F.getType());

      SmallVector<unsigned, 8> Bits;
      for (auto i : candidate.bv)
        Bits.push_back(i);
      Metadata *BVNode =
          ConstantAsMetadata::get(ConstantDataArray::get(F.getContext(), Bits));
      Metadata *CalleeNode = ConstantAsMetadata::get(NewCallee);
      Metadata *CallerNode = ConstantAsMetadata::get(NewCaller);
      MDNodes.push_back(
          MDNode::get(F.getContext(), {BVNode, CalleeNode, CallerNode}));
    }
  }

  if (Changed) {
    Metadata *OrigNode = ConstantAsMetadata::get(&F);
    MDNode *ListNode = MDNode::get(F.getContext(), MDNodes);
    MDNode *TopNode = MDNode::get(F.getContext(), {OrigNode, ListNode});
    F.getParent()
        ->getOrInsertNamedMetadata("smout.extracted.functions")
        ->addOperand(TopNode);
  }

  return Changed;
}

PreservedAnalyses OutliningExtractorPass::run(Module &m,
                                              ModuleAnalysisManager &am) {
  FunctionAnalysisManager &fam =
      am.getResult<FunctionAnalysisManagerModuleProxy>(m).getManager();
  // Only run on functions that already existed when we began the pass.
  SmallVector<Function *, 1> Functions;
  for (Function &F : m)
    if (!F.isDeclaration())
      Functions.push_back(&F);
  bool Changed = false;
  for (Function *F : Functions) {
    OutliningDependenceResults &OutDep =
        fam.getResult<OutliningDependenceAnalysis>(*F);
    auto &OutCands = fam.getResult<OutliningCandidatesAnalysis>(*F);
    Changed = runOnFunction(*F, OutDep, OutCands) || Changed;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

OutliningExtractorWrapperPass::OutliningExtractorWrapperPass()
    : ModulePass(ID) {}

bool OutliningExtractorWrapperPass::runOnModule(Module &M) {
  // Only run on functions that already existed when we began the pass.
  SmallVector<Function *, 1> Functions;
  for (Function &F : M)
    if (!F.isDeclaration())
      Functions.push_back(&F);
  bool Changed = false;
  for (Function *F : Functions) {
    OutliningDependenceResults &OutDep =
        getAnalysis<OutliningDependenceWrapperPass>(*F).getOutDep();
    auto &OutCands =
        getAnalysis<OutliningCandidatesWrapperPass>(*F).getOutCands();
    Changed = runOnFunction(*F, OutDep, OutCands) || Changed;
  }
  return Changed;
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
