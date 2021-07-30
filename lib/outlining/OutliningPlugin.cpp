#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>

#include "outlining/Candidates.h"
#include "outlining/Dependence.h"
#include "outlining/Extractor.h"
#include "outlining/FalseMemorySSA.h"
#include "outlining/SizeModel.h"

using namespace bcdb;
using namespace llvm;

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "OutliningPlugin", "0.1",
          [](PassBuilder &builder) {
            builder.registerAnalysisRegistrationCallback(
                [](FunctionAnalysisManager &am) {
                  am.registerPass([] { return FalseMemorySSAAnalysis(); });
                  am.registerPass([] { return OutliningCandidatesAnalysis(); });
                  am.registerPass([] { return OutliningDependenceAnalysis(); });
                  am.registerPass([] { return SizeModelAnalysis(); });
                });
            builder.registerPipelineParsingCallback(
                [](StringRef name, FunctionPassManager &fpm,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (name == "print<false-memory-ssa>") {
                    fpm.addPass(FalseMemorySSAPrinterPass(dbgs()));
                    return true;
                  }
                  if (name == "print<outlining-candidates>") {
                    fpm.addPass(OutliningCandidatesPrinterPass(dbgs()));
                    return true;
                  }
                  if (name == "print<outlining-dependence>") {
                    fpm.addPass(OutliningDependencePrinterPass(dbgs()));
                    return true;
                  }
                  if (name == "print<size-model>") {
                    fpm.addPass(SizeModelPrinterPass(dbgs()));
                    return true;
                  }
                  return false;
                });
            builder.registerPipelineParsingCallback(
                [](StringRef name, ModulePassManager &fpm,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (name == "outlining-extractor") {
                    fpm.addPass(OutliningExtractorPass());
                    return true;
                  }
                  return false;
                });
          }};
}
