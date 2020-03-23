#ifndef BCDB_SPLIT_H
#define BCDB_SPLIT_H

#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Error.h>
#include <memory>

namespace llvm {
class GlobalObject;
class LLVMContext;
class Module;
class StringRef;
} // end namespace llvm

namespace bcdb {

class SplitLoader {
public:
  virtual llvm::Expected<std::unique_ptr<llvm::Module>>
  loadFunction(llvm::StringRef Name) = 0;
  virtual llvm::Expected<std::unique_ptr<llvm::Module>> loadRemainder() = 0;
  virtual ~SplitLoader() {}
};

class SplitSaver {
public:
  virtual llvm::Error saveFunction(std::unique_ptr<llvm::Module> Module,
                                   llvm::StringRef Name) = 0;
  virtual llvm::Error saveRemainder(std::unique_ptr<llvm::Module> Module) = 0;
  virtual ~SplitSaver() {}
};

class Melter {
  std::unique_ptr<llvm::Module> M;
  llvm::IRMover Mover;

public:
  Melter(llvm::LLVMContext &Context);
  llvm::Error Merge(std::unique_ptr<llvm::Module> MPart);
  llvm::Module &GetModule();
};

class Splitter {
public:
  Splitter(llvm::Module &M);
  std::unique_ptr<llvm::Module> SplitGlobal(llvm::GlobalObject *GO);
  void Finish() {}

private:
  llvm::Module &M;
};

llvm::Expected<std::unique_ptr<llvm::Module>> JoinModule(SplitLoader &Loader);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
