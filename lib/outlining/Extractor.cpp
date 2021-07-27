#include "outlining/Extractor.h"

#include <llvm/ADT/SparseBitVector.h>
#include <llvm/Analysis/PostDominators.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

#include "outlining/Candidates.h"

using namespace bcdb;
using namespace llvm;

OutliningCalleeExtractor::OutliningCalleeExtractor(
    Function &function, const OutliningDependenceResults &deps,
    const SparseBitVector<> &bv)
    : function(function), deps(deps), bv(bv) {
  const auto &Nodes = deps.Nodes;
  const auto &NodeIndices = deps.NodeIndices;

  if (!deps.isOutlinable(bv))
    report_fatal_error("Specified nodes cannot be outlined",
                       /* gen_crash_diag */ false);

  auto addInputValue = [&](Value *v) {
    if (Argument *arg = dyn_cast<Argument>(v))
      arg_inputs.set(arg->getArgNo());
    else if (NodeIndices.count(v))
      external_inputs.set(NodeIndices.lookup(v));
    else
      assert(isa<Constant>(v) && "impossible");
  };

  // Determine which nodes inside the new callee will need to have their
  // results passed back to the new caller.
  for (size_t i = 0; i < Nodes.size(); i++) {
    if (bv.test(i))
      continue;
    if (PHINode *phi = dyn_cast<PHINode>(Nodes[i])) {
      SmallPtrSet<Value *, 8> phi_incoming;
      for (unsigned j = 0; j < phi->getNumIncomingValues(); j++) {
        Value *v = phi->getIncomingValue(j);
        if (bv.test(NodeIndices.lookup(
                phi->getIncomingBlock(j)->getTerminator()))) {
          // May depend on control flow in the callee.
          phi_incoming.insert(v);
        } else if (NodeIndices.count(v)) {
          // Only depends on control flow from the caller, but may need a value
          // from the callee.
          external_outputs.set(NodeIndices.lookup(v));
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
        external_outputs.set(NodeIndices.lookup(*phi_incoming.begin()));
      }
    } else {
      external_outputs |= deps.DataDepends[i];
    }
  }
  external_outputs &= bv;
  external_outputs |= output_phis;

  // Determine which function arguments and other nodes need to be passed as
  // arguments to the new callee.
  for (auto i : bv) {
    if (PHINode *phi = dyn_cast<PHINode>(Nodes[i])) {
      SmallPtrSet<Value *, 8> phi_incoming;
      for (unsigned j = 0; j < phi->getNumIncomingValues(); j++) {
        if (bv.test(NodeIndices.lookup(
                phi->getIncomingBlock(j)->getTerminator()))) {
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
      external_inputs |= deps.DataDepends[i];
      arg_inputs |= deps.ArgDepends[i];
    }
  }
  // The next statement must be executed after all calls to addInputValue().
  external_inputs.intersectWithComplement(bv);
  external_inputs |= input_phis;

  for (auto i : bv) {
    if (isa<BasicBlock>(Nodes[i]))
      outlined_blocks.set(i);
    else if (Instruction *I = dyn_cast<Instruction>(Nodes[i]))
      outlined_blocks.set(NodeIndices.lookup(I->getParent()));
  }
}

unsigned OutliningCalleeExtractor::getNumArgs() const {
  return arg_inputs.count() + external_inputs.count();
}

unsigned OutliningCalleeExtractor::getNumReturnValues() const {
  return external_outputs.count();
}

void OutliningCalleeExtractor::getArgTypes(
    SmallVectorImpl<Type *> &types) const {
  for (auto i : arg_inputs)
    types.push_back((function.arg_begin() + i)->getType());
  for (auto i : external_inputs)
    types.push_back(deps.Nodes[i]->getType());
}

void OutliningCalleeExtractor::getResultTypes(
    SmallVectorImpl<Type *> &types) const {
  for (auto i : external_outputs)
    types.push_back(deps.Nodes[i]->getType());
}

Function *OutliningCalleeExtractor::createDeclaration() {
  if (new_callee)
    return new_callee;

  const auto &nodes = deps.Nodes;

  // Determine the type of the outlined function.
  // FIXME: Canonicalize arguments and return values by reordering them. Also
  // make all pointers opaque.
  SmallVector<Type *, 8> result_types, arg_types;
  for (auto i : external_outputs)
    result_types.push_back(nodes[i]->getType());
  Type *result_type = StructType::get(function.getContext(), result_types);
  for (auto i : arg_inputs)
    arg_types.push_back((function.arg_begin() + i)->getType());
  for (auto i : external_inputs)
    arg_types.push_back(nodes[i]->getType());
  FunctionType *callee_type =
      FunctionType::get(result_type, arg_types, /* isVarArg */ false);

  std::string new_name;
  raw_string_ostream new_name_os(new_name);
  new_name_os << function.getName() << ".outlined.";
  // Use assembler-friendly characters "." and "_".
  deps.printSet(new_name_os, bv, ".", "_");
  new_callee =
      Function::Create(callee_type, GlobalValue::ExternalLinkage,
                       new_name_os.str() + ".callee", function.getParent());

  // Pass more arguments and return values in registers.
  // TODO: experiment to check whether the code is actually smaller this way.
  new_callee->setCallingConv(CallingConv::Fast);

  // Add function attributes.
  //
  // Note that we don't add the uwtable attribute. If the function never throws
  // exceptions, that means the unwinding table will be omitted, making the
  // function smaller but also preventing backtraces from working correctly.
  new_callee->addFnAttr(Attribute::MinSize);
  new_callee->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  if (function.hasPersonalityFn()) {
    for (auto i : bv) {
      if (Instruction *ins = dyn_cast<Instruction>(nodes[i])) {
        if (ins->isEHPad()) {
          new_callee->setPersonalityFn(function.getPersonalityFn());
          break;
        }
      }
    }
  }

  return new_callee;
}

Function *OutliningCalleeExtractor::createDefinition() {
  if (!new_callee)
    createDeclaration();
  if (!new_callee->isDeclaration())
    return new_callee;

  const auto &PDT = deps.PDT;
  const auto &Nodes = deps.Nodes;
  const auto &NodeIndices = deps.NodeIndices;

  // Add entry and exit blocks. We need a new entry block because we might be
  // outlining a loop, and LLVM prohibits the entry block from being part of a
  // loop. We need a new exit block so we can set up the return value.
  BasicBlock *EntryBlock =
      BasicBlock::Create(new_callee->getContext(), "outline_entry", new_callee);
  BasicBlock *ExitBlock = BasicBlock::Create(new_callee->getContext(),
                                             "outline_return", new_callee);

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
  Function::arg_iterator new_arg_iter = new_callee->arg_begin();
  for (auto i : arg_inputs) {
    Argument &orig_arg = *(function.arg_begin() + i);
    Argument &new_arg = *new_arg_iter++;
    VMap[&orig_arg] = &new_arg;
    new_arg.setName(orig_arg.getName());
  }
  for (auto i : external_inputs) {
    Value &orig_value = *Nodes[i];
    Argument &new_value = *new_arg_iter++;
    if (input_phis.test(i))
      input_phi_map[&orig_value] = &new_value;
    else
      VMap[&orig_value] = &new_value;
    new_value.setName(orig_value.getName());
  }
  assert(new_arg_iter == new_callee->arg_end());

  // Map blocks in the original function to blocks in the outlined function.
  DenseMap<BasicBlock *, BasicBlock *> BBMap;
  for (BasicBlock &BB : function) {
    if (!NodeIndices.count(&BB))
      continue; // unreachable block
    // We may be outlining a branch (either a conditional branch, or an
    // implicit unconditional branch) that goes to block BB even though we
    // aren't outlining any instructions in BB. In that case, use the
    // postdominator tree to skip blocks until we find one that actually is
    // being outlined.
    BasicBlock *PDom = &BB;
    while (PDom && !outlined_blocks.test(NodeIndices.lookup(PDom)))
      PDom = PDT[PDom]->getIDom()->getBlock();
    if (!PDom) {
      VMap[&BB] = ExitBlock;
      continue;
    }
    if (!BBMap.count(PDom))
      BBMap[PDom] = BasicBlock::Create(new_callee->getContext(),
                                       PDom->getName(), new_callee);
    VMap[&BB] = BBMap[PDom];
  }

  // Set up PHI nodes that were not chosen for outlining, but which depend on
  // control flow in the outlined set. This needs to be done before other
  // instructions are added.
  for (auto i : output_phis) {
    PHINode *orig_phi = cast<PHINode>(Nodes[i]);
    PHINode *new_phi =
        PHINode::Create(orig_phi->getType(), 0, orig_phi->getName(),
                        BBMap[orig_phi->getParent()]);
    VMap[orig_phi] = new_phi;
    for (unsigned j = 0; j < orig_phi->getNumIncomingValues(); ++j) {
      BasicBlock *orig_pred = orig_phi->getIncomingBlock(j);
      if (!NodeIndices.count(orig_pred))
        continue; // unreachable block
      if (bv.test(NodeIndices.lookup(orig_pred->getTerminator())))
        new_phi->addIncoming(orig_phi->getIncomingValue(j), orig_pred);
    }
    VM.remapInstruction(*new_phi);
  }

  // Jump from the entry block to the first actual outlined block.
  BasicBlock *FirstBlock =
      cast<BasicBlock>(Nodes[outlined_blocks.find_first()]);
  BranchInst::Create(BBMap[FirstBlock], EntryBlock);

  // Clone the selected instructions into the outlined function.
  for (auto i : bv) {
    if (Instruction *orig_ins = dyn_cast<Instruction>(Nodes[i])) {
      BasicBlock *new_block = BBMap[orig_ins->getParent()];
      Instruction *new_ins = orig_ins->clone();
      VMap[orig_ins] = new_ins;
      new_ins->setName(orig_ins->getName());
      new_block->getInstList().push_back(new_ins);
    }
  }

  // Remap instruction operands.
  for (auto i : bv) {
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
        if (!bv.test(NodeIndices.lookup(orig_pred->getTerminator()))) {
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

  // Set up the return instruction.
  Value *ResultValue = UndefValue::get(new_callee->getReturnType());
  unsigned ResultI = 0;
  for (auto i : external_outputs)
    ResultValue = InsertValueInst::Create(ResultValue, VMap[Nodes[i]],
                                          {ResultI++}, "", ExitBlock);
  ReturnInst::Create(new_callee->getContext(), ResultValue, ExitBlock);

  // If we're outlining an infinite loop, we shouldn't have an exit block.
  if (pred_empty(ExitBlock))
    ExitBlock->eraseFromParent();

  return new_callee;
}

OutliningCallerExtractor::OutliningCallerExtractor(
    Function &function, const OutliningDependenceResults &deps,
    const std::vector<SparseBitVector<>> &bvs)
    : function(function), deps(deps), bvs(bvs) {
  SparseBitVector<> all_bv;
  for (const auto &bv : bvs) {
    if (all_bv.intersects(bv))
      report_fatal_error("can't outline overlapping candidates");
    all_bv |= bv;
    callees.emplace_back(function, deps, bv);
  }
}

Function *OutliningCallerExtractor::createDefinition() {
  if (new_caller)
    return new_caller;

  new_caller = CloneFunction(&function, vmap, nullptr);

  std::string new_name;
  raw_string_ostream new_name_os(new_name);
  new_name_os << function.getName() << ".outlined.";
  // Use assembler-friendly characters "." and "_".
  bool new_name_first = true;
  for (const auto &bv : bvs) {
    if (!new_name_first)
      new_name_os << "__";
    new_name_first = false;
    deps.printSet(new_name_os, bv, ".", "_");
  }
  new_caller->setName(new_name_os.str() + ".caller");

  unreachable_block =
      BasicBlock::Create(new_caller->getContext(), "unreachable", new_caller);
  new UnreachableInst(new_caller->getContext(), unreachable_block);
  i32_type = IntegerType::get(new_caller->getContext(), 32);

  for (size_t m = 0; m < callees.size(); ++m)
    handleSingleCallee(bvs[m], callees[m]);

  // Replace uses of outlined instructions.
  //
  // XXX: replaceAllUsesWith changes the values stored in vmap! So we can't use
  // vmap after executing this loop.
  for (const auto &item : replacements) {
    item.second->takeName(item.first);
    item.first->replaceAllUsesWith(item.second);
  }

  // Actually delete outlined instructions.
  for (Instruction *ins : insns_to_delete) {
    ins->replaceAllUsesWith(UndefValue::get(ins->getType()));
    ins->eraseFromParent();
  }

  return new_caller;
}

BasicBlock *OutliningCallerExtractor::findNextBlock(BasicBlock *bb) {
  const auto &pdt = deps.PDT;
  auto pdom = pdt[bb]->getIDom();
  if (!pdom || !pdom->getBlock())
    return nullptr;

  // Breadth-first search.
  DenseMap<BasicBlock *, BasicBlock *> pred;
  pred[bb] = nullptr;
  SmallVector<BasicBlock *, 8> queue;
  queue.emplace_back(bb);
  for (size_t i = 0; i < queue.size(); ++i) {
    // Note that queue is modified within this loop.
    BasicBlock *block = queue[i];
    if (block == pdom->getBlock())
      break;
    for (BasicBlock *succ : successors(block)) {
      if (!pred.count(succ)) {
        pred[succ] = block;
        queue.emplace_back(succ);
      }
    }
  }

  BasicBlock *block = pdom->getBlock();
  while (pred[block] != bb) {
    assert(pred[block] && "no path found to postdominator!");
    block = pred[block];
  }
  return block;
}

void OutliningCallerExtractor::handleSingleCallee(
    const SparseBitVector<> &bv, OutliningCalleeExtractor &callee) {
  const auto &nodes = deps.Nodes;
  const auto &node_indices = deps.NodeIndices;

  Value *outlining_point = nodes[bv.find_first()];
  Instruction *insertion_point;
  if (isa<Instruction>(outlining_point)) {
    insertion_point = cast<Instruction>(vmap[outlining_point]);
  } else if (isa<BasicBlock>(outlining_point)) {
    insertion_point = cast<BasicBlock>(vmap[outlining_point])->getFirstNonPHI();
  } else {
    // MemoryPhi is impossible here because it has a forced dependence on the
    // corresponding BasicBlock.
    llvm_unreachable("Impossible node type");
  }

  // Construct the call.
  SmallVector<Value *, 8> args;
  for (auto i : callee.arg_inputs)
    args.push_back(new_caller->arg_begin() + i);
  for (auto i : callee.external_inputs)
    args.push_back(vmap[nodes[i]]);
  Function *new_callee = callee.createDeclaration();
  CallInst *call_inst = CallInst::Create(new_callee->getFunctionType(),
                                         new_callee, args, "", insertion_point);
  call_inst->setCallingConv(new_callee->getCallingConv());

  // Extract outputs.
  DenseMap<size_t, Value *> output_values;
  unsigned result_i = 0;
  for (auto i : callee.external_outputs)
    output_values[i] =
        ExtractValueInst::Create(call_inst, {result_i++}, "", insertion_point);

  // Handle PHI nodes in the caller that depend on control flow within the
  // callee. Other phi values will be rewritten by replaceAllUsesWith below.
  for (size_t i : callee.output_phis) {
    PHINode *orig_phi = cast<PHINode>(nodes[i]);
    PHINode *new_phi = cast<PHINode>(vmap[orig_phi]);
    for (unsigned j = 0; j < orig_phi->getNumIncomingValues(); ++j)
      if (bv.test(node_indices.lookup(
              orig_phi->getIncomingBlock(j)->getTerminator())))
        new_phi->setIncomingValue(j, output_values[i]);
  }

  // Prepare to delete outlined instructions. We don't actually delete any
  // BasicBlocks or CFG edges, because that would complicate the handling of
  // PHI nodes. Any unreachable or empty blocks can be deleted later by the
  // SimplifyCFG pass.
  for (auto i : bv) {
    if (Instruction *orig_ins = dyn_cast<Instruction>(nodes[i])) {
      if (isa<PHINode>(orig_ins) && orig_ins->getParent() == outlining_point) {
        // PHI node in the first block being outlined. We shouldn't necessarily
        // delete it; if it depends on control flow in the caller, we need to
        // pass the result of the PHI into the callee.
        continue;
      }
      insns_to_delete.push_back(cast<Instruction>(vmap[orig_ins]));
      if (orig_ins->isTerminator()) {
        // The terminator's input values may be deleted, but we don't want to
        // change the CFG, so we replace it with a switch instruction that has
        // all the same successors.

        // The switch instruction should always jump to the next block on the
        // path to the postdominator.
        BasicBlock *next = findNextBlock(orig_ins->getParent());

        // There are a few ways next can be nullptr:
        //
        // 1. The terminator is a RetInst or an "unwind to caller" instruction.
        // 2. The terminator is an UnreachableInst.
        // 3. The terminator is a ResumeInst.
        // 4. The terminator is part of an infinite loop.
        // 5. The terminator is a conditional branch with at least one path to
        //    one of cases 1-5.
        //
        // Case 1 is prohibited by OutliningDependence. In case 5, all
        // instructions that can be executed after the branch will be (directly
        // or indirectly) control-dependent on it, and OutliningDependence
        // requires all such instructions be outlined whenever the branch is
        // outlined; this is only allowed if all paths after the branch
        // eventually lead to cases 2-4. And no path leading to cases 2-4 will
        // ever return normally from the outlined callee. So regardless of
        // which case we have, this instruction is unreachable.
        BasicBlock *default_block =
            next ? cast<BasicBlock>(vmap[next]) : unreachable_block;

        unsigned next_index = 0;
        auto switch_inst = SwitchInst::Create(
            ConstantInt::get(i32_type, next_index++), default_block, 0,
            cast<BasicBlock>(vmap[orig_ins->getParent()]));
        for (BasicBlock *succ : successors(orig_ins->getParent())) {
          if (succ == next)
            continue;
          switch_inst->addCase(ConstantInt::get(i32_type, next_index++),
                               cast<BasicBlock>(vmap[succ]));
        }
      }
    }
  }

  for (auto i : callee.external_outputs) {
    if (!bv.test(i)) {
      // PHI node in the block after the call; handled separately.
      continue;
    }
    replacements[vmap[nodes[i]]] = output_values[i];
  }
}

static bool runOnFunction(Function &function, OutliningDependenceResults &deps,
                          OutliningCandidates &OutCands) {
  std::vector<SparseBitVector<>> bvs;
  for (OutliningCandidates::Candidate &candidate : OutCands.Candidates)
    bvs.emplace_back(candidate.bv);

  if (bvs.empty())
    return false;

  OutliningCallerExtractor caller_extractor(function, deps, bvs);
  for (auto &callee : caller_extractor.callees)
    callee.createDefinition();
  Function *NewCaller = caller_extractor.createDefinition();
  NewCaller->takeName(&function);
  function.replaceAllUsesWith(NewCaller);
  function.eraseFromParent();

  return true;
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
    OutliningDependenceResults &deps =
        fam.getResult<OutliningDependenceAnalysis>(*F);
    auto &OutCands = fam.getResult<OutliningCandidatesAnalysis>(*F);
    Changed = runOnFunction(*F, deps, OutCands) || Changed;
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
    OutliningDependenceResults &deps =
        getAnalysis<OutliningDependenceWrapperPass>(*F).getOutDep();
    auto &OutCands =
        getAnalysis<OutliningCandidatesWrapperPass>(*F).getOutCands();
    Changed = runOnFunction(*F, deps, OutCands) || Changed;
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
