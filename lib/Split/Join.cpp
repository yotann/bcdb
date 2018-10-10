#include "bcdb/Split.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
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

  // List all the function stubs and delete their bodies.
  SmallVector<StringRef, 0> Stubs;
  for (Function &F : *M) {
    LinkageMap[F.getName()] = F.getLinkage();
    F.setLinkage(GlobalValue::ExternalLinkage);
    if (!F.isDeclaration()) {
      Stubs.push_back(F.getName());
      F.dropAllReferences();
    }
  }

  // Link all function definitions.
  Linker L(*M);
  for (StringRef Name : Stubs) {
    Function *F = M->getFunction(Name);
    auto Comdat = F->getComdat();

    std::unique_ptr<Module> MPart = Loader.loadFunction(Name);
    for (Function &FDef : *MPart) {
      if (!FDef.isDeclaration()) {
        FDef.setName(Name);
        FDef.copyAttributesFrom(F);
      } else {
        // Declarations must have unnamed_addr, otherwise the linker will strip
        // unnamed_addr or local_unnamed_addr when linking.
        FDef.setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
      }
    }
    L.linkInModule(std::move(MPart));

    F = M->getFunction(Name);
    F->setComdat(Comdat);
  }

  // Restore linkage types for globals.
  for (GlobalValue &GV : M->globals())
    GV.setLinkage(LinkageMap[GV.getName()]);
  for (GlobalValue &F : *M)
    F.setLinkage(LinkageMap[F.getName()]);

  return M;
}
