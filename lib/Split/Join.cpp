#include "bcdb/Split.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <memory>

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

bcdb::Melter::Melter(llvm::LLVMContext &Context)
    : M(std::make_unique<Module>("melted", Context)), Mover(*M) {}

Error bcdb::Melter::Merge(std::unique_ptr<Module> MPart) {
  Function *Def = nullptr;
  for (Function &F : *MPart) {
    if (!F.isDeclaration()) {
      if (Def) {
        return make_error<StringError>("multiple functions in function module",
                                       errc::invalid_argument);
      }
      Def = &F;
    }
  }
  return Mover.move(
      std::move(MPart), {Def}, [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
      /* LinkModuleInlineAsm */ false,
#endif
      /* IsPerformingImport */ false);
}

Module &Melter::GetModule() { return *M; }

Expected<std::unique_ptr<Module>> bcdb::JoinModule(SplitLoader &Loader) {
  Expected<std::unique_ptr<Module>> ModuleOrErr = Loader.loadRemainder();
  if (!ModuleOrErr)
    return ModuleOrErr.takeError();
  std::unique_ptr<Module> M = std::move(*ModuleOrErr);

  // Make all globals external so function modules can link to them.
  StringMap<GlobalValue::LinkageTypes> LinkageMap;
  for (GlobalObject &GO : M->global_objects()) {
    LinkageMap[GO.getName()] = GO.getLinkage();
    GO.setLinkage(GlobalValue::ExternalLinkage);
  }

  // List all the function stubs and declarations.
  SmallVector<Function *, 0> InFunctions;
  for (Function &F : *M)
    InFunctions.push_back(&F);

  // Link all function definitions.
  IRMover Mover(*M);
  SmallVector<Function *, 0> OutFunctions;
  for (Function *Stub : InFunctions) {
    if (!isStub(*Stub)) {
      OutFunctions.push_back(Stub);
      continue;
    }

    // Find the function definition.
    StringRef Name = Stub->getName();
    Expected<std::unique_ptr<Module>> MPartOrErr = Loader.loadFunction(Name);
    if (!MPartOrErr)
      return MPartOrErr.takeError();
    std::unique_ptr<Module> MPart = std::move(*MPartOrErr);
    Function *Def = nullptr;
    for (Function &F : *MPart) {
      if (!F.isDeclaration()) {
        if (Def) {
          return make_error<StringError>(
              "multiple functions in function module " + Name,
              errc::invalid_argument);
        }
        Def = &F;
      }
    }

    // Copy linker information from the stub.
    Def->setName(Name);
    assert(Def->getName() == Name && "name conflict");
    Def->copyAttributesFrom(Stub);
    Def->setComdat(Stub->getComdat());

    // Move the definition into the main module.
    if (Error Err = Mover.move(
            std::move(MPart), {Def},
            [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
            /* LinkModuleInlineAsm */ false,
#endif
            /* IsPerformingImport */ false))
      return std::move(Err);
    assert(M->getFunction(Name) != Stub && "stub was not replaced");
    OutFunctions.push_back(M->getFunction(Name));
  }

  // Restore linkage types for globals.
  for (GlobalObject &GO : M->global_objects())
    GO.setLinkage(LinkageMap[GO.getName()]);

  // Reorder the functions to match their original order. This has no effect on
  // correctness, but makes it easier to compare the joined module with the
  // original one.
  for (Function *F : OutFunctions)
    F->removeFromParent();
  M->getFunctionList().insert(M->getFunctionList().end(), OutFunctions.begin(),
                              OutFunctions.end());

  return std::move(M);
}
