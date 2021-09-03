#ifndef BCDB_OUTLINING_SIZEMODEL_H
#define BCDB_OUTLINING_SIZEMODEL_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>

namespace llvm {
class AnalysisUsage;
class Instruction;
class Module;
class raw_ostream;
} // end namespace llvm

namespace bcdb {

using llvm::AnalysisInfoMixin;
using llvm::AnalysisKey;
using llvm::AnalysisUsage;
using llvm::DenseMap;
using llvm::Function;
using llvm::FunctionAnalysisManager;
using llvm::FunctionPass;
using llvm::Instruction;
using llvm::Module;
using llvm::Optional;
using llvm::PassInfoMixin;
using llvm::PreservedAnalyses;
using llvm::raw_ostream;

// Calculates the estimated compiled size of each instruction in a function.
class SizeModelResults {
public:
  SizeModelResults(Function &f);

  void print(raw_ostream &os) const;

  // Calculate the estimated size, in bytes, of a function, given an estimate
  // of the size of its instructions. Assumes a new return instruction will
  // need to be added.
  unsigned estimateSize(unsigned instructions_size, bool has_callee) const;

  // The estimated size, in bytes, of each of the Function's instructions after
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

  // The estimated total size, in bytes, of this particular function, including
  // the base function size, the size of all instructions, and the padding
  // between this function and the next one.
  unsigned this_function_total_size;

private:
  Function &f;
};

class SizeModelAnalysis : public AnalysisInfoMixin<SizeModelAnalysis> {
  friend AnalysisInfoMixin<SizeModelAnalysis>;

  static AnalysisKey Key;

public:
  using Result = SizeModelResults;

  SizeModelResults run(Function &f, FunctionAnalysisManager &am);
};

class SizeModelPrinterPass : public PassInfoMixin<SizeModelPrinterPass> {
  raw_ostream &os;

public:
  explicit SizeModelPrinterPass(raw_ostream &os) : os(os) {}

  PreservedAnalyses run(Function &f, FunctionAnalysisManager &am);
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
