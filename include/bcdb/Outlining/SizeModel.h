#ifndef BCDB_OUTLINING_SIZEMODEL_H
#define BCDB_OUTLINING_SIZEMODEL_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/Pass.h>

namespace llvm {
class AnalysisUsage;
class Instruction;
class Module;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using llvm::AnalysisUsage;
using llvm::DenseMap;
using llvm::Instruction;
using llvm::Module;
using llvm::ModulePass;
using llvm::Optional;
using llvm::raw_ostream;

// Calculates the estimated compiled size of each instruction in a module.
class SizeModelResults {
public:
  SizeModelResults(Module &m);

  void print(raw_ostream &os) const;

  // The estimated size, in bytes, of each of the Module's instructions after
  // compilation. Unusual values are possible; for example, an instruction's
  // size may be 0 if it is merged with another instruction during compilation.
  DenseMap<Instruction *, unsigned> instruction_sizes;

  Module &m;
};

struct SizeModelWrapperPass : public ModulePass {
  SizeModelWrapperPass();

  static char ID;

  bool runOnModule(Module &m) override;
  void print(raw_ostream &os, const Module *m = nullptr) const override;
  void getAnalysisUsage(AnalysisUsage &au) const override;
  void releaseMemory() override;
  void verifyAnalysis() const override;
  SizeModelResults &getSizeModel() { return *size_model; }

private:
  Optional<SizeModelResults> size_model;
};

} // end namespace bcdb

#endif // BCDB_OUTLINING_SIZEMODEL_H
