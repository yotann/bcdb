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
using llvm::Function;
using llvm::FunctionPass;
using llvm::Instruction;
using llvm::Module;
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

  // The estimated size, in bytes, of a typical call instruction.
  unsigned call_instruction_size;

  // The estimated size, in bytes, of a minimal function with no callees. This
  // includes things like the return instruction and the average size of
  // padding between functions.
  unsigned function_size_without_callees;

  // The estimated size, in bytes, of a function that has one or more callees.
  // In addition to function_size_without_callees, this includes things like
  // frame pointer management instructions and exception handling frame data.
  unsigned function_size_with_callees;

private:
  Module &m;
};

struct SizeModelWrapperPass : public FunctionPass {
  SizeModelWrapperPass();

  static char ID;

  bool runOnFunction(Function &func) override;
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
