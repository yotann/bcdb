#ifndef BCDB_COSTMODEL_H
#define BCDB_COSTMODEL_H

#include <map>
#include <vector>

namespace llvm {
class Function;
class Instruction;
class StringRef;
} // end namespace llvm

namespace bcdb {

enum class CostItem {
  Function,
  FunctionArg,
  FunctionUnwindTable,
  ReturnVoid,
  ReturnNonvoid,
  BranchUncond,
  BranchCond,
  Switch,
  Call,
  Invoke,
  AddSub,
  FAddSubMul,
  Alloca,
  Load,
  Store,
  GetElementPtr,
  PHI,
  Cast,
  Select,
  ShuffleVectorSize,
  OtherTerminator,
  OtherInstruction,
};

std::vector<CostItem> getAllCostItems();
llvm::StringRef getCostItemName(CostItem Item);

class CostModel {
  std::map<CostItem, unsigned> Items;
  friend struct CostVisitor;

public:
  CostModel();
  void addFunction(const llvm::Function &F);
  void addInstruction(const llvm::Instruction &I);
  const std::map<CostItem, unsigned> getItems() const { return Items; }
};

} // namespace bcdb

#endif // BCDB_COSTMODEL_H
