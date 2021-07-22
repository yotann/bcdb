#include "outlining/SizeModel.h"

#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/CodeGen/Passes.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/Debugify.h>
#include <memory>
#include <vector>

#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

namespace {
// Prints a module with comments showing the size model results, for debugging.
class SizeModelWriter : public AssemblyAnnotationWriter {
  const SizeModelResults *size_model;

public:
  SizeModelWriter(const SizeModelResults *size_model)
      : size_model(size_model) {}

  void printInfoComment(const Value &value,
                        formatted_raw_ostream &os) override {
    if (const Instruction *ins = dyn_cast<Instruction>(&value)) {
      auto it = size_model->instruction_sizes.find(ins);
      if (it == size_model->instruction_sizes.end())
        return;
      os.PadToColumn(60) << formatv("; {0} bytes", it->second);
    }
  }
};
} // end anonymous namespace

namespace {
// Track sizes of machine instructions.
//
// Normally, MCStreamer instances are used to write assembly files or object
// files. SizingStreamer doesn't write any files; it just tracks debug line
// numbers, and calculates the total size of all instructions associated with a
// given line number.
struct SizingStreamer : public MCStreamer {
  std::vector<unsigned> &sizes;
  MCCodeEmitter &mce;
  const MCSubtargetInfo &sti;
  unsigned current_line = 0;

  explicit SizingStreamer(std::vector<unsigned> &sizes, MCContext &context,
                          MCCodeEmitter &mce, const MCSubtargetInfo &sti)
      : MCStreamer(context), sizes(sizes), mce(mce), sti(sti) {}

  // Must implement (pure virtual function).
#if LLVM_VERSION_MAJOR >= 11
  bool emitSymbolAttribute(MCSymbol *, MCSymbolAttr) override {
#else
  bool EmitSymbolAttribute(MCSymbol *, MCSymbolAttr) override {
#endif
    return false; // not supported
  }

  // Must implement (pure virtual function).
#if LLVM_VERSION_MAJOR >= 11
  void emitCommonSymbol(MCSymbol *, uint64_t, unsigned) override {}
#else
  void EmitCommonSymbol(MCSymbol *, uint64_t, unsigned) override {}
#endif

  // Must implement (pure virtual function).
#if LLVM_VERSION_MAJOR >= 11
  void emitZerofill(MCSection *, MCSymbol *, uint64_t Size,
                    unsigned ByteAlignment, SMLoc Loc) override {}
#else
  void EmitZerofill(MCSection *, MCSymbol *, uint64_t Size,
                    unsigned ByteAlignment, SMLoc Loc) override {}
#endif

#if LLVM_VERSION_MAJOR >= 11
  void emitInstruction(const MCInst &inst,
                       const MCSubtargetInfo &sti) override {
    MCStreamer::emitInstruction(inst, sti);
#else
  void EmitInstruction(const MCInst &inst,
                       const MCSubtargetInfo &sti) override {
    MCStreamer::EmitInstruction(inst, sti);
#endif

    SmallVector<char, 256> buffer;
    raw_svector_ostream os(buffer);
    SmallVector<MCFixup, 4> fixups;
    mce.encodeInstruction(inst, os, fixups, sti);
    sizes[current_line] += os.str().size();
  }

#if LLVM_VERSION_MAJOR >= 11
  void emitDwarfLocDirective(unsigned file_no, unsigned line, unsigned column,
                             unsigned flags, unsigned isa,
                             unsigned discriminator,
                             StringRef filename) override {
    MCStreamer::emitDwarfLocDirective(file_no, line, column, flags, isa,
                                      discriminator, filename);
#else
  void EmitDwarfLocDirective(unsigned file_no, unsigned line, unsigned column,
                             unsigned flags, unsigned isa,
                             unsigned discriminator,
                             StringRef filename) override {
    MCStreamer::EmitDwarfLocDirective(file_no, line, column, flags, isa,
                                      discriminator, filename);
#endif
    current_line = line;
    if (current_line >= sizes.size())
      sizes.resize(current_line + 1);
  }

#if LLVM_VERSION_MAJOR >= 11
  void emitCVLocDirective(unsigned function_id, unsigned file_no, unsigned line,
                          unsigned column, bool prologue_end, bool is_stmt,
                          StringRef filename, SMLoc loc) override {
    MCStreamer::emitCVLocDirective(function_id, file_no, line, column,
                                   prologue_end, is_stmt, filename, loc);
#else
  void EmitCVLocDirective(unsigned function_id, unsigned file_no, unsigned line,
                          unsigned column, bool prologue_end, bool is_stmt,
                          StringRef filename, SMLoc loc) override {
    MCStreamer::EmitCVLocDirective(function_id, file_no, line, column,
                                   prologue_end, is_stmt, filename, loc);
#endif
    current_line = line;
    if (current_line >= sizes.size())
      sizes.resize(current_line + 1);
  }
};
} // end anonymous namespace

SizeModelResults::SizeModelResults(Function &f) : f(f) {
  // We need to run transformations on the module in order to compile it and
  // measure sizes, but we shouldn't modify the original module. So we make a
  // clone of it.
  ValueToValueMapTy vmap;
  auto cloned = CloneModule(*f.getParent(), vmap,
                            [&](const GlobalValue *gv) { return gv == &f; });

  // Debugify doesn't do anything if llvm.dbg.cu already exists.
  auto dbg = cloned->getNamedMetadata("llvm.dbg.cu");
  if (dbg)
    cloned->eraseNamedMetadata(dbg);

  // Create fake debug information, which assigns a different line number to
  // each IR instruction in the module. We use these line numbers to track
  // which machine instructions correspond to which IR instructions.
  std::unique_ptr<ModulePass> debugify(createDebugifyModulePass());
  debugify->runOnModule(*cloned);

  // Record the line number mapping now, before we start transforming the
  // cloned module.
  DenseMap<unsigned, Instruction *> line_to_instruction;
  for (auto &bb : f) {
    for (auto &i_orig : bb) {
      Value *v = vmap[&i_orig];
      if (!v)
        continue; // Dead code.
      Instruction *ins = dyn_cast<Instruction>(v);
      if (!ins)
        continue; // PHINode simplified to a constant.
      assert(ins->getDebugLoc() && "should be guaranteed by debugify");
      unsigned line = ins->getDebugLoc().getLine();
      assert(line > 0 && "should be guaranteed by debugify");
      line_to_instruction[line] = &i_orig;
    }
  }

  // Now we actually compile the module, using our custom MCStreamer
  // implementation that calculates instruction sizes without actually writing
  // a file. We have to do a lot of steps manually in order to use a custom
  // MCStreamer!

  // Based on llvm/tools/llc/llc.cpp:
  std::string error;
  const Target *target =
      TargetRegistry::lookupTarget(cloned->getTargetTriple(), error);
  if (!target) {
    report_fatal_error("Can't find target triple: " + error);
  }
  TargetOptions options;
  std::unique_ptr<TargetMachine> target_machine(
      target->createTargetMachine(cloned->getTargetTriple(), "", "", options,
                                  None, None, CodeGenOpt::Default));
  TargetLibraryInfoImpl tlii(Triple(cloned->getTargetTriple()));
  LLVMTargetMachine &llvmtm = static_cast<LLVMTargetMachine &>(*target_machine);
  legacy::PassManager pm;
  pm.add(new TargetLibraryInfoWrapperPass(tlii));

  // Based on LLVMTargetMachine::addPassesToEmitMC:
  auto mmiwp = new MachineModuleInfoWrapperPass(&llvmtm);
  MCContext *context = &mmiwp->getMMI().getContext();
  const MCSubtargetInfo &sti = *llvmtm.getMCSubtargetInfo();
  const MCRegisterInfo &mri = *llvmtm.getMCRegisterInfo();
  MCCodeEmitter *mce =
      target->createMCCodeEmitter(*llvmtm.getMCInstrInfo(), mri, *context);
  if (!mce) {
    report_fatal_error("Can't create machine code emitter");
  }

  // Based on llvm's addPassesToGenerateCode:
  TargetPassConfig *pass_config = llvmtm.createPassConfig(pm);
  pass_config->setDisableVerify(true);
  pm.add(pass_config);
  pm.add(mmiwp);
  if (pass_config->addISelPasses())
    report_fatal_error("addISelPasses failed");
  pass_config->addMachinePasses();
  pass_config->setInitialized();

  // TODO: Our custom MCStreamer should work for all targets, including x86.
  // But most other targets support TargetInstrInfo::getInstSizeInBytes(),
  // which we could use in a custom MachineFunctionPass without setting up
  // AsmPrinter. Would there be any advantages to doing that for non-x86
  // targets?

  // Actually set up our custom MCStreamer, and perform compilation!
  std::vector<unsigned> sizes;
  std::unique_ptr<MCStreamer> asm_streamer(
      new SizingStreamer(sizes, *context, *mce, sti));
  target->createNullTargetStreamer(*asm_streamer);
  FunctionPass *printer =
      target->createAsmPrinter(llvmtm, std::move(asm_streamer));
  if (!printer)
    report_fatal_error("createAsmPrinter failed");
  pm.add(printer);
  pm.add(createFreeMachineFunctionPass());
  pm.run(*cloned);

  // Now we actually take the per-line-number sizes calculated by
  // SizingStreamer, find the corresponding IR instructions in the cloned
  // module, and map them to the original instructions.
  //
  // TODO: Sometimes the obvious mapping isn't quite right.
  //
  // - When multiple IR instructions are combined into one machine instruction,
  //   the size is only assigned to one of the IR instructions and the others
  //   get a size of 0. It would be better to spread the size across all of
  //   them, if we can heuristically detect which instructions were combined.
  //
  // - Some machine instructions don't have any corresponding IR instruction.
  //   (They get a line number of 0.) This can happen with machine instructions
  //   that e.g. clear a register for future use. It would be better to
  //   heuristically find a good place to assign that size. (Maybe by tracking
  //   the next instruction that uses the output of the unassigned
  //   instruction?)
  //
  // - On wasm32 and riscv32, the size of the prologue instructions gets added
  //   to the size of the first instruction.
  //
  // - On wasm32, the size of the end_function instruction gets added to the
  //   size of the last instruction.
  for (auto &item : line_to_instruction) {
    unsigned line = item.first;
    Instruction *ins = item.second;
    instruction_sizes[ins] = line < sizes.size() ? sizes[line] : 0;
  }

  // TODO: take options that affect these sizes, like the code model, into
  // account.
  unsigned ret_size = 0;           // Return instruction size.
  unsigned extra_func_size = 0;    // Extra bytes for each instruction.
  unsigned eh_frame_size = 16;     // Most targets use .eh_frame.
  unsigned fp_management_size = 0; // Frame pointer management instructions.
  unsigned function_alignment = 0;
  switch (target_machine->getTargetTriple().getArch()) {
  case Triple::ArchType::arm:
  case Triple::ArchType::armeb:
    // Functions are aligned to 4 bytes, but that's almost always a no-op.
    call_instruction_size = 4;
    ret_size = 4;
    eh_frame_size = 8; // uses .ARM.exidx, not .eh_frame
    fp_management_size = 8;
    break;
  case Triple::ArchType::aarch64:
  case Triple::ArchType::aarch64_be:
    // Functions are aligned to 4 bytes, but that's almost always a no-op.
    call_instruction_size = 4;
    ret_size = 4;
    fp_management_size = 8;
    break;
  case Triple::ArchType::riscv32:
  case Triple::ArchType::riscv64:
    // Functions are aligned to 4 bytes, but that's almost always a no-op.
    call_instruction_size = 8;
    ret_size = 4;
    fp_management_size = 16;
    break;
  case Triple::ArchType::thumb:
  case Triple::ArchType::thumbeb:
    // Functions are aligned to 2 bytes, but that's almost always a no-op.
    call_instruction_size = 4;
    ret_size = 2;
    eh_frame_size = 8; // uses .ARM.exidx, not .eh_frame
    fp_management_size = 4;
    break;
  case Triple::ArchType::wasm32:
  case Triple::ArchType::wasm64:
    call_instruction_size = 6;
    ret_size = 1;
    extra_func_size =
        2; // Estimate 2 bytes function type in the Function Section.
    extra_func_size += 1; // Estimate 1 byte code size in the Code Section.
    eh_frame_size = 0;
    fp_management_size = 0;
    break;
  case Triple::ArchType::x86_64:
    call_instruction_size = 5;
    ret_size = 1;
    function_alignment = 16;
    break;
  default:
    llvm::report_fatal_error("unsupported target for size estimation");
  }
  function_size_without_callees =
      ret_size + extra_func_size + (function_alignment + 1) / 2;
  function_size_with_callees =
      function_size_without_callees + eh_frame_size + fp_management_size;
}

void SizeModelResults::print(raw_ostream &os) const {
  os << "; estimated call instruction size: " << call_instruction_size
     << " bytes\n";
  os << "; estimated function size without callees: "
     << function_size_without_callees << " bytes\n";
  os << "; estimated function size with callees: " << function_size_with_callees
     << " bytes\n";
  os << "\n";
  SizeModelWriter writer(this);
  f.print(os, &writer);
}

AnalysisKey SizeModelAnalysis::Key;

SizeModelResults SizeModelAnalysis::run(Function &f,
                                        FunctionAnalysisManager &am) {
  return SizeModelResults(f);
}

PreservedAnalyses SizeModelPrinterPass::run(Function &f,
                                            FunctionAnalysisManager &am) {
  auto &size_model = am.getResult<SizeModelAnalysis>(f);
  os << "SizeModel for function: " << f.getName() << "\n";
  size_model.print(os);
  return PreservedAnalyses::all();
}

SizeModelWrapperPass::SizeModelWrapperPass() : FunctionPass(ID) {}

bool SizeModelWrapperPass::runOnFunction(Function &func) {
  size_model.emplace(func);
  return false;
}

void SizeModelWrapperPass::print(raw_ostream &os, const Module *M) const {
  size_model->print(os);
}

void SizeModelWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

void SizeModelWrapperPass::releaseMemory() { size_model.reset(); }

void SizeModelWrapperPass::verifyAnalysis() const {
  assert(false && "unimplemented");
}

char SizeModelWrapperPass::ID = 0;
static RegisterPass<SizeModelWrapperPass>
    X("size-model", "Size Model Analysis Pass", false, true);
