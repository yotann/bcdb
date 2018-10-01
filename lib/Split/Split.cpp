#include "bcdb/Split.h"

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

using namespace bcdb;
using namespace llvm;

void bcdb::SplitModule(
    std::unique_ptr<Module> M,
    function_ref<void(StringRef, StringRef, std::unique_ptr<Module> MPart)>
        ModuleCallback) {

  // Make sure all globals are named so we can link everything back together
  // later.
  nameUnamedGlobals(*M);

  legacy::PassManager PM;
  PM.add(createStripDeadPrototypesPass());

  for (Function &F : *M) {
    if (!F.isDeclaration()) {
      // Create a new module containing only this function.
      ValueToValueMapTy VMap;
#if LLVM_VERSION_MAJOR >= 7
      const Module &MArg = *M;
#else
      const Module *MArg = M.get();
#endif
      std::unique_ptr<Module> MPart(CloneModule(
          MArg, VMap, [&](const GlobalValue *GV) { return GV == &F; }));

      // Clear unneeded data from the new module.
      VMap[&F]->setName("");
      PM.run(*MPart);
      MPart->setSourceFileName("");
      MPart->setModuleInlineAsm("");
      NamedMDNode *NMD = MPart->getNamedMetadata("llvm.ident");
      if (NMD)
        MPart->eraseNamedMetadata(NMD);

      ModuleCallback("functions", F.getName(), std::move(MPart));

      // Delete the function from the old module.
      F.deleteBody();
      F.setComdat(nullptr);
    }
  }

  ModuleCallback("remainder", "module", std::move(M));
}
