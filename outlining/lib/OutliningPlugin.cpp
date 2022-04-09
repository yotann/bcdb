#include <optional>

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

// Must be a module pass so it can run on optnone functions.
namespace {
class AddFunctionAttrPass : public PassInfoMixin<AddFunctionAttrPass> {
public:
  AddFunctionAttrPass(Attribute::AttrKind kind) : kind(kind) {}
  PreservedAnalyses run(Module &m, ModuleAnalysisManager &am);
  Attribute::AttrKind kind;
};
} // namespace

PreservedAnalyses AddFunctionAttrPass::run(Module &m, ModuleAnalysisManager &) {
  for (Function &f : m)
    if (!f.hasFnAttribute(kind))
      f.addFnAttr(kind);
  return PreservedAnalyses::none();
}

// Must be a module pass so it can run on optnone functions.
namespace {
class RemoveFunctionAttrPass : public PassInfoMixin<RemoveFunctionAttrPass> {
public:
  RemoveFunctionAttrPass(Attribute::AttrKind kind) : kind(kind) {}
  PreservedAnalyses run(Module &m, ModuleAnalysisManager &am);
  Attribute::AttrKind kind;
};
} // namespace

PreservedAnalyses RemoveFunctionAttrPass::run(Module &m,
                                              ModuleAnalysisManager &) {
  for (Function &f : m)
    if (f.hasFnAttribute(kind))
      f.removeFnAttr(kind);
  return PreservedAnalyses::none();
}

namespace {
class RelaxForAlivePass : public PassInfoMixin<RelaxForAlivePass> {
public:
  PreservedAnalyses run(Function &f, FunctionAnalysisManager &am);
};
} // namespace

PreservedAnalyses RelaxForAlivePass::run(Function &f,
                                         FunctionAnalysisManager &) {
  for (BasicBlock &bb : f) {
    for (Instruction &i : bb) {
      // Remove metadata that Alive2 doesn't support. These metadata types
      // provide constraints on the behavior of the program, so it's safe to
      // remove them, but potentially unsafe to introduce them.
      i.setMetadata(LLVMContext::MD_align, nullptr);
      i.setMetadata(LLVMContext::MD_dereferenceable, nullptr);
      i.setMetadata(LLVMContext::MD_dereferenceable_or_null, nullptr);
      i.setMetadata(LLVMContext::MD_invariant_group, nullptr);
      i.setMetadata(LLVMContext::MD_invariant_load, nullptr);
      i.setMetadata(LLVMContext::MD_nonnull, nullptr);
      i.setMetadata(LLVMContext::MD_nontemporal, nullptr);
    }
  }
  return PreservedAnalyses::none();
}

static std::optional<Attribute::AttrKind>
parseAttributeKindPassName(StringRef name, StringRef pass_name) {
  if (!name.consume_front(pass_name) || !name.consume_front("<") ||
      !name.consume_back(">"))
    return {};
  auto kind = Attribute::getAttrKindFromName(name);
  if (kind == Attribute::None)
    return {};
  return kind;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "OutliningPlugin", "0.1",
          [](PassBuilder &builder) {
            builder.registerAnalysisRegistrationCallback(
                [](FunctionAnalysisManager &am) {
                  am.registerPass([] { return FalseMemorySSAAnalysis(); });
                  am.registerPass([] {
                    return OutliningCandidatesAnalysis(
                        OutliningCandidatesOptions::getFromCommandLine());
                  });
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
                  if (name == "relax-for-alive") {
                    fpm.addPass(RelaxForAlivePass());
                    return true;
                  }
                  return false;
                });
            builder.registerPipelineParsingCallback(
                [](StringRef name, ModulePassManager &mpm,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (name == "outlining-extractor") {
                    mpm.addPass(OutliningExtractorPass());
                    return true;
                  }
                  if (auto kind = parseAttributeKindPassName(
                          name, "add-function-attr")) {
                    mpm.addPass(AddFunctionAttrPass(*kind));
                    return true;
                  }
                  if (auto kind = parseAttributeKindPassName(
                          name, "remove-function-attr")) {
                    mpm.addPass(RemoveFunctionAttrPass(*kind));
                    return true;
                  }
                  return false;
                });
          }};
}
