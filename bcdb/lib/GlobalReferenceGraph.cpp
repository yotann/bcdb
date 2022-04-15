#include "bcdb/GlobalReferenceGraph.h"

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Use.h>
#include <memory>
#include <utility>

using namespace bcdb;
using namespace llvm;

SmallPtrSet<GlobalValue *, 8>
bcdb::FindGlobalReferences(GlobalValue *Root,
                           SmallPtrSetImpl<GlobalValue *> *ForcedSameModule) {
  SmallPtrSet<GlobalValue *, 8> Result;
  SmallVector<Value *, 8> Todo;

  if (ForcedSameModule) {
#if LLVM_VERSION_MAJOR >= 14
    if (GlobalAlias *GA = dyn_cast<GlobalAlias>(Root))
      ForcedSameModule->insert(GA->getAliaseeObject());
    else if (GlobalIFunc *GIF = dyn_cast<GlobalIFunc>(Root))
      ForcedSameModule->insert(GIF->getResolverFunction());
#else
    if (GlobalIndirectSymbol *GIS = dyn_cast<GlobalIndirectSymbol>(Root))
      ForcedSameModule->insert(GIS->getBaseObject());
#endif
  }

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
    if (BlockAddress *BA = dyn_cast<BlockAddress>(V))
      if (ForcedSameModule)
        ForcedSameModule->insert(BA->getFunction());
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
