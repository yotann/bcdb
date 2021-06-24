#include "bcdb/Outlining/CostModel.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <magic_enum.hpp>

using namespace bcdb;
using namespace llvm;

std::vector<CostItem> bcdb::getAllCostItems() {
  auto Array = magic_enum::enum_values<CostItem>();
  return std::vector(Array.begin(), Array.end());
}

llvm::StringRef bcdb::getCostItemName(CostItem Item) {
  auto Name = magic_enum::enum_name(Item);
  return llvm::StringRef(Name.data(), Name.size());
}

CostModel::CostModel() {}

void CostModel::addFunction(const Function &F) {
  Items[CostItem::Function]++;
  Items[CostItem::FunctionArg] += F.arg_size();
  if (F.hasUWTable())
    Items[CostItem::FunctionUnwindTable]++;
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      addInstruction(I);
}

namespace bcdb {
struct CostVisitor : public InstVisitor<CostVisitor> {
  CostVisitor(CostModel *Model) : Model(Model) {}

  void visitAddInst(BinaryOperator &I) { Model->Items[CostItem::AddSub]++; }

  void visitSubInst(BinaryOperator &I) { Model->Items[CostItem::AddSub]++; }

  void visitFAddInst(BinaryOperator &I) {
    Model->Items[CostItem::FAddSubMul]++;
  }

  void visitFSubInst(BinaryOperator &I) {
    Model->Items[CostItem::FAddSubMul]++;
  }

  void visitFMulInst(BinaryOperator &I) {
    Model->Items[CostItem::FAddSubMul]++;
  }

  void visitICmpInst(ICmpInst &I) { Model->Items[CostItem::AddSub]++; }

  void visitFCmpInst(FCmpInst &I) { Model->Items[CostItem::FAddSubMul]++; }

  void visitAllocaInst(AllocaInst &I) { Model->Items[CostItem::Alloca]++; }

  void visitLoadInst(LoadInst &I) { Model->Items[CostItem::Load]++; }

  void visitStoreInst(StoreInst &I) { Model->Items[CostItem::Store]++; }

  void visitGetElementPtrInst(GetElementPtrInst &I) {
    Model->Items[CostItem::GetElementPtr]++;
  }

  void visitPHINode(PHINode &I) { Model->Items[CostItem::PHI]++; }

  void visitCastInst(CastInst &I) { Model->Items[CostItem::Cast]++; }

  void visitSelectInst(SelectInst &I) { Model->Items[CostItem::Select]++; }

  void visitShuffleVectorInst(ShuffleVectorInst &I) {
    Model->Items[CostItem::ShuffleVectorSize] += I.getType()->getElementCount().getValue();
  }

  void visitCallInst(CallInst &I) { Model->Items[CostItem::Call]++; }

  void visitInvokeInst(InvokeInst &I) { Model->Items[CostItem::Invoke]++; }

  void visitReturnInst(ReturnInst &I) {
    if (I.getType()->isVoidTy() || I.getType()->isEmptyTy())
      Model->Items[CostItem::ReturnVoid]++;
    else
      Model->Items[CostItem::ReturnNonvoid]++;
  }

  void visitSwitchInst(SwitchInst &I) { Model->Items[CostItem::Switch]++; }

  void visitUnreachableInst(UnreachableInst &I) {}

  void visitBranchInst(BranchInst &I) {
    if (I.isConditional())
      Model->Items[CostItem::BranchCond]++;
    else
      Model->Items[CostItem::BranchUncond]++;
  }

  void visitTerminator(Instruction &I) {
    Model->Items[CostItem::OtherTerminator]++;
  }

  void visitInstruction(Instruction &I) {
    Model->Items[CostItem::OtherInstruction]++;
  }

  CostModel *Model;
};
} // end namespace bcdb

void CostModel::addInstruction(const Instruction &I) {
  CostVisitor(this).visit(const_cast<Instruction *>(&I));
#if 0
  switch (I.getOpcode()) {
  } else if (const BinaryOperator *BinOp = dyn_cast<BinaryOperator>(&I)) {
    if (BinOp->getOpcode() == Instruction::Add || BinOp->getOpcode() == Instruction::Sub)
      Items[CostItem::AddSub]++;
    else
      Items[CostItem::OtherBinary]++;
  } else {
    Items[CostItem::OtherInstruction]++;
  }
#endif
}
