#include "Util.h"

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Use.h>

using namespace bcdb;
using namespace llvm;

SmallPtrSet<GlobalValue *, 8> bcdb::FindGlobalReferences(GlobalValue *Root) {
  SmallPtrSet<GlobalValue *, 8> Result;
  SmallVector<Value *, 8> Todo;

  // TODO: visit function/instruction metadata?
  for (auto &Op : Root->operands())
    Todo.push_back(Op);
  if (Function *F = dyn_cast<Function>(Root))
    for (BasicBlock &BB : *F)
      for (Instruction &I : BB)
        for (const Use &Op : I.operands())
          Todo.push_back(Op);

  while (!Todo.empty()) {
    Value *V = Todo.pop_back_val();
    // TODO: check for MetadataAsValue?
    if (V == Root)
      continue;
    if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
      Result.insert(GV);
    } else if (Constant *C = dyn_cast<Constant>(V)) {
      for (auto &Op : C->operands())
        Todo.push_back(Op);
    }
  }
  return Result;
}

GlobalReferenceGraph::GlobalReferenceGraph(Module &M) : Nodes(), Edges() {
  Nodes.emplace_back(this, nullptr);
  for (GlobalValue &GV :
       concat<GlobalValue>(M.global_objects(), M.aliases(), M.ifuncs())) {
    Nodes.emplace_back(this, &GV);
    auto &Targets = Edges[&GV];
    for (const auto &Ref : FindGlobalReferences(&GV))
      Targets.emplace_back(this, Ref);
  }
}
