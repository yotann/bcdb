#include <string>
#include <utility>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

using namespace llvm;

#if LLVM_VERSION_MAJOR <= 5
using ToolOutputFile = tool_output_file;
#endif

static cl::opt<std::string> InputFilename(cl::Positional, cl::Required,
                                          cl::desc("<input bitcode file>"));

static cl::opt<std::string> OutputDirectory(cl::Positional, cl::Required,
                                            cl::desc("<output directory>"));

static void SplitModule(
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

int main(int argc, const char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "Module splitting");

  LLVMContext Context;
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  SplitModule(std::move(M), [&](StringRef dir, StringRef file,
                                std::unique_ptr<Module> MPart) {

    std::error_code EC;
    std::string Filename = (OutputDirectory + "/" + dir + "/" + file).str();
    std::unique_ptr<ToolOutputFile> Out(
        new ToolOutputFile(Filename, EC, sys::fs::F_None));
    if (EC) {
      errs() << EC.message() << '\n';
      exit(1);
    }

    verifyModule(*MPart);
#if LLVM_VERSION_MAJOR >= 7
    WriteBitcodeToFile(*MPart, Out->os());
#else
    WriteBitcodeToFile(MPart.get(), Out->os());
#endif
    Out->keep();
  });

  return 0;
}
