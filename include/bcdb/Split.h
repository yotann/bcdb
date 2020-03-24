#ifndef BCDB_SPLIT_H
#define BCDB_SPLIT_H

#include <llvm/ADT/StringMap.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/Linker/IRMover.h>
#include <llvm/Support/Error.h>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class GlobalObject;
class LLVMContext;
class Module;
class StringRef;
} // end namespace llvm

namespace bcdb {

class Joiner {
public:
  Joiner(llvm::Module &Remainder);
  void JoinGlobal(llvm::StringRef Name, std::unique_ptr<llvm::Module> MPart);
  void Finish();

private:
  llvm::Module *M;
  llvm::StringMap<llvm::GlobalValue::LinkageTypes> LinkageMap;
  llvm::IRMover Mover;
  std::vector<std::string> GlobalNames;
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

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
