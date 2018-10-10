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
  for (GlobalValue &GV : M->globals()) {
    LinkageMap[GV.getName()] = GV.getLinkage();
    GV.setLinkage(GlobalValue::ExternalLinkage);
  }

  // List all the function stubs and mark them external.
  SmallVector<StringRef, 0> Stubs;
  for (Function &F : *M) {
    if (!F.isDeclaration()) {
      LinkageMap[F.getName()] = F.getLinkage();
      F.setLinkage(GlobalValue::ExternalLinkage);
      Stubs.push_back(F.getName());
    }
  }

  // Link all function definitions.
  IRMover Mover(*M);
  for (StringRef Name : Stubs) {
    Function *Stub = M->getFunction(Name);

    // Find the function definition.
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
                         /* IsPerformingImport */ false);
    handleAllErrors(std::move(E), [](const ErrorInfoBase &E) {
      errs() << E.message() << '\n';
      report_fatal_error("error during joining");
    });
    assert(M->getFunction(Name) != Stub && "stub was not replaced");
  }

  // Restore linkage types for globals.
  for (GlobalValue &GV : M->globals())
    GV.setLinkage(LinkageMap[GV.getName()]);
  for (Function &F : *M)
    if (!F.isDeclaration())
      F.setLinkage(LinkageMap[F.getName()]);

  return M;
}
