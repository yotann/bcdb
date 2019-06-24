#ifndef BCDB_BCDB_H
#define BCDB_BCDB_H

#include "memodb/memodb.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Error.h>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class Module;
class StringRef;
} // end namespace llvm

namespace bcdb {

class BCDB {
  llvm::LLVMContext Context;
  std::unique_ptr<memodb_db> db;

public:
  BCDB(std::unique_ptr<memodb_db> db);
  static llvm::Error Init(llvm::StringRef uri);
  static llvm::Expected<std::unique_ptr<BCDB>> Open(llvm::StringRef uri);

  ~BCDB();
  llvm::Error Add(llvm::StringRef Name, std::unique_ptr<llvm::Module> M);
  llvm::Expected<std::unique_ptr<llvm::Module>> Get(llvm::StringRef Name);
  llvm::Expected<std::unique_ptr<llvm::Module>>
  GetFunctionById(llvm::StringRef Id);
  llvm::Expected<std::vector<std::string>> ListModules();
  llvm::Expected<std::vector<std::string>>
  ListFunctionsInModule(llvm::StringRef Name);
  llvm::Expected<std::vector<std::string>> ListAllFunctions();
  llvm::LLVMContext &GetContext() { return Context; }
  llvm::Error Delete(llvm::StringRef Name);
};

} // end namespace bcdb

#endif // BCDB_BCDB_H
