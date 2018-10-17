#include "bcdb/Split.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Error.h>
#include <memory>

using namespace bcdb;
using namespace llvm;

std::unique_ptr<Module> bcdb::JoinModule(SplitLoader &Loader) {
  std::unique_ptr<Module> M = Loader.loadRemainder();

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
    if (Stub->isDeclaration()) {
      OutFunctions.push_back(Stub);
      continue;
    }

    // Find the function definition.
    StringRef Name = Stub->getName();
    std::unique_ptr<Module> MPart = Loader.loadFunction(Name);
    Function *Def = nullptr;
    for (Function &F : *MPart) {
      if (!F.isDeclaration()) {
        assert(!Def && "multiple functions in function module");
        Def = &F;
      }
    }

    // Copy linker information from the stub.
    Def->setName(Name);
    assert(Def->getName() == Name && "name conflict");
    Def->copyAttributesFrom(Stub);
    Def->setComdat(Stub->getComdat());

    // Move the definition into the main module.
    Error E = Mover.move(std::move(MPart), {Def},
                         [](GlobalValue &GV, IRMover::ValueAdder Add) {},
#if LLVM_VERSION_MAJOR <= 4
                         /* LinkModuleInlineAsm */ false,
#endif
                         /* IsPerformingImport */ false);
    handleAllErrors(std::move(E), [](const ErrorInfoBase &E) {
      errs() << E.message() << '\n';
      report_fatal_error("error during joining");
    });
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

  return M;
}
