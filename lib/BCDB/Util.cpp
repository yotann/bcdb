#include "Util.h"

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

  if (GlobalIndirectSymbol *GIS = dyn_cast<GlobalIndirectSymbol>(Root))
    if (ForcedSameModule)
      ForcedSameModule->insert(GIS->getBaseObject());

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

// Copied from llvm/lib/Transforms/Utils/CloneModule.cpp, but changed to create
// function definitions before global variable definitions. This is necessary
// to handle blockaddresses in global variable initializers.

static void copyComdat(GlobalObject *Dst, const GlobalObject *Src) {
  const Comdat *SC = Src->getComdat();
  if (!SC)
    return;
  Comdat *DC = Dst->getParent()->getOrInsertComdat(SC->getName());
  DC->setSelectionKind(SC->getSelectionKind());
  Dst->setComdat(DC);
}

std::unique_ptr<Module> bcdb::CloneModuleCorrectly(const Module &M) {
  ValueToValueMapTy VMap;
  return CloneModuleCorrectly(M, VMap);
}

std::unique_ptr<Module> bcdb::CloneModuleCorrectly(const Module &M,
                                                   ValueToValueMapTy &VMap) {
  return CloneModuleCorrectly(M, VMap,
                              [](const GlobalValue *GV) { return true; });
}

std::unique_ptr<Module> bcdb::CloneModuleCorrectly(
    const Module &M, ValueToValueMapTy &VMap,
    function_ref<bool(const GlobalValue *)> ShouldCloneDefinition) {
  // First off, we need to create the new module.
  std::unique_ptr<Module> New =
      std::make_unique<Module>(M.getModuleIdentifier(), M.getContext());
  New->setSourceFileName(M.getSourceFileName());
  New->setDataLayout(M.getDataLayout());
  New->setTargetTriple(M.getTargetTriple());
  New->setModuleInlineAsm(M.getModuleInlineAsm());

  // Loop over all of the global variables, making corresponding globals in the
  // new module.  Here we add them to the VMap and to the new Module.  We
  // don't worry about attributes or initializers, they will come later.
  //
  for (Module::const_global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I) {
    GlobalVariable *GV = new GlobalVariable(
        *New, I->getValueType(), I->isConstant(), I->getLinkage(),
        static_cast<Constant *>(nullptr), I->getName(),
        static_cast<GlobalVariable *>(nullptr), I->getThreadLocalMode(),
        I->getType()->getAddressSpace());
    GV->copyAttributesFrom(&*I);
    VMap[&*I] = GV;
  }

  // Loop over the functions in the module, making external functions as before
  for (const Function &I : M) {
    Function *NF =
        Function::Create(cast<FunctionType>(I.getValueType()), I.getLinkage(),
                         I.getAddressSpace(), I.getName(), New.get());
    NF->copyAttributesFrom(&I);
    VMap[&I] = NF;
  }

  // Loop over the aliases in the module
  for (Module::const_alias_iterator I = M.alias_begin(), E = M.alias_end();
       I != E; ++I) {
    if (!ShouldCloneDefinition(&*I)) {
      // An alias cannot act as an external reference, so we need to create
      // either a function or a global variable depending on the value type.
      // FIXME: Once pointee types are gone we can probably pick one or the
      // other.
      GlobalValue *GV;
      if (I->getValueType()->isFunctionTy())
        GV = Function::Create(cast<FunctionType>(I->getValueType()),
                              GlobalValue::ExternalLinkage,
                              I->getAddressSpace(), I->getName(), New.get());
      else
        GV = new GlobalVariable(*New, I->getValueType(), false,
                                GlobalValue::ExternalLinkage, nullptr,
                                I->getName(), nullptr, I->getThreadLocalMode(),
                                I->getType()->getAddressSpace());
      VMap[&*I] = GV;
      // We do not copy attributes (mainly because copying between different
      // kinds of globals is forbidden), but this is generally not required for
      // correctness.
      continue;
    }
    auto *GA = GlobalAlias::create(I->getValueType(),
                                   I->getType()->getPointerAddressSpace(),
                                   I->getLinkage(), I->getName(), New.get());
    GA->copyAttributesFrom(&*I);
    VMap[&*I] = GA;
  }

  // Copy over function bodies now...
  //
  for (const Function &I : M) {
    if (I.isDeclaration())
      continue;

    Function *F = cast<Function>(VMap[&I]);
    if (!ShouldCloneDefinition(&I)) {
      // Skip after setting the correct linkage for an external reference.
      F->setLinkage(GlobalValue::ExternalLinkage);
      // Personality function is not valid on a declaration.
      F->setPersonalityFn(nullptr);
      continue;
    }

    Function::arg_iterator DestI = F->arg_begin();
    for (Function::const_arg_iterator J = I.arg_begin(); J != I.arg_end();
         ++J) {
      DestI->setName(J->getName());
      VMap[&*J] = &*DestI++;
    }

    SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
    CloneFunctionInto(F, &I, VMap, /*ModuleLevelChanges=*/true, Returns);

    if (I.hasPersonalityFn())
      F->setPersonalityFn(MapValue(I.getPersonalityFn(), VMap));

    copyComdat(F, &I);
  }

  // Now that all of the things that global variable initializer can refer to
  // have been created, loop through and copy the global variable referrers
  // over...  We also set the attributes on the global now.
  //
  for (Module::const_global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I) {
    if (I->isDeclaration())
      continue;

    GlobalVariable *GV = cast<GlobalVariable>(VMap[&*I]);
    if (!ShouldCloneDefinition(&*I)) {
      // Skip after setting the correct linkage for an external reference.
      GV->setLinkage(GlobalValue::ExternalLinkage);
      continue;
    }
    if (I->hasInitializer())
      GV->setInitializer(MapValue(I->getInitializer(), VMap));

    SmallVector<std::pair<unsigned, MDNode *>, 1> MDs;
    I->getAllMetadata(MDs);
    for (auto MD : MDs)
      GV->addMetadata(MD.first,
                      *MapMetadata(MD.second, VMap, RF_MoveDistinctMDs));

    copyComdat(GV, &*I);
  }

  // And aliases
  for (Module::const_alias_iterator I = M.alias_begin(), E = M.alias_end();
       I != E; ++I) {
    // We already dealt with undefined aliases above.
    if (!ShouldCloneDefinition(&*I))
      continue;
    GlobalAlias *GA = cast<GlobalAlias>(VMap[&*I]);
    if (const Constant *C = I->getAliasee())
      GA->setAliasee(MapValue(C, VMap));
  }

  // And named metadata....
  const auto *LLVM_DBG_CU = M.getNamedMetadata("llvm.dbg.cu");
  for (Module::const_named_metadata_iterator I = M.named_metadata_begin(),
                                             E = M.named_metadata_end();
       I != E; ++I) {
    const NamedMDNode &NMD = *I;
    NamedMDNode *NewNMD = New->getOrInsertNamedMetadata(NMD.getName());
    if (&NMD == LLVM_DBG_CU) {
      // Do not insert duplicate operands.
      SmallPtrSet<const void *, 8> Visited;
      for (const auto *Operand : NewNMD->operands())
        Visited.insert(Operand);
      for (const auto *Operand : NMD.operands()) {
        auto *MappedOperand = MapMetadata(Operand, VMap);
        if (Visited.insert(MappedOperand).second)
          NewNMD->addOperand(MappedOperand);
      }
    } else {
      for (unsigned i = 0, e = NMD.getNumOperands(); i != e; ++i)
        NewNMD->addOperand(MapMetadata(NMD.getOperand(i), VMap));
    }
  }

  return New;
}
