#include "bcdb/Split.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <memory>

#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

static bool isStub(const Function &F) {
  if (F.isDeclaration() || F.size() != 1)
    return false;
  const BasicBlock &BB = F.getEntryBlock();
  if (BB.size() != 1 || !isa<UnreachableInst>(BB.front()))
    return false;
  return true;
}

Function &bcdb::getSoleDefinition(Module &MPart) {
  Function *Def = nullptr;
  for (Function &F : MPart) {
    if (!F.isDeclaration()) {
      if (Def)
        report_fatal_error("multiple functions in function module");
      Def = &F;
    }
  }
  if (!Def)
    report_fatal_error("missing function in function module");
  return *Def;
}

bcdb::Melter::Melter(llvm::LLVMContext &Context)
    : M(std::make_unique<Module>("melted", Context)), Mover(*M) {}

Error bcdb::Melter::Merge(std::unique_ptr<Module> MPart) {
  Function &Def = getSoleDefinition(*MPart);
  return Mover.move(
      std::move(MPart), {&Def}, [](GlobalValue &GV, IRMover::ValueAdder Add) {},
      /* IsPerformingImport */ false);
}

Module &Melter::GetModule() { return *M; }

Joiner::Joiner(llvm::Module &Remainder) : M(&Remainder), Mover(*M) {
  // Make all globals external so function modules can link to them.
  for (GlobalValue &GV :
       concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs())) {
    LinkageMap[GV.getName()] = GV.getLinkage();
    GV.setLinkage(GlobalValue::ExternalLinkage);
  }

  // List all the function stubs and declarations.
  for (Function &F : *M)
    GlobalNames.push_back(std::string(F.getName()));
}

static AttributeList copyTypeAttributes(LLVMContext &C, AttributeList Source,
                                        AttributeList Attrs) {
  assert(Attrs.getNumAttrSets() == Source.getNumAttrSets());
  for (unsigned i = 0; i < Source.getNumAttrSets(); ++i) {
    for (const Attribute &attr : Source.getAttributes(i)) {
      if (attr.isTypeAttribute()) {
        Type *Ty = attr.getValueAsType();
        if (Ty) {
          Attrs = Attrs.removeAttribute(C, i, attr.getKindAsEnum());
          Attrs = Attrs.addAttribute(C, i, attr);
        }
      }
    }
  }
  return Attrs;
}

void Joiner::JoinGlobal(llvm::StringRef Name,
                        std::unique_ptr<llvm::Module> MPart) {
  Function *Stub = M->getFunction(Name);
  assert(isStub(*Stub));

  // Copy linker information from the stub.
  Function *Def = &getSoleDefinition(*MPart);
  Def->setName(Name);
  assert(Def->getName() == Name && "name conflict");
  AttributeList OldAttrs = Def->getAttributes();
  Def->copyAttributesFrom(Stub);
  Def->setAttributes(
      copyTypeAttributes(Def->getContext(), OldAttrs, Def->getAttributes()));
  Def->setComdat(Stub->getComdat());

  // Move the definition into the main module.
  ExitOnError Err("JoinGlobal");
  Err(Mover.move(
      std::move(MPart), {Def}, [](GlobalValue &GV, IRMover::ValueAdder Add) {},
      /* IsPerformingImport */ false));
  assert(M->getFunction(Name) != Stub && "stub was not replaced");
}

void Joiner::Finish() {
  // Restore linkage types for globals.
  for (GlobalValue &GV :
       concat<GlobalValue>(M->global_objects(), M->aliases(), M->ifuncs()))
    GV.setLinkage(LinkageMap[GV.getName()]);

  // Reorder the functions to match their original order. This has no effect on
  // correctness, but makes it easier to compare the joined module with the
  // original one.
  SmallVector<Function *, 0> OrderedFunctions;
  for (auto &Name : GlobalNames) {
    Function *F = M->getFunction(Name);
    OrderedFunctions.push_back(F);
    F->removeFromParent();
  }
  M->getFunctionList().insert(M->getFunctionList().end(),
                              OrderedFunctions.begin(), OrderedFunctions.end());
}
