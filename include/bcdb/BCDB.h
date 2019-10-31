#ifndef BCDB_BCDB_H
#define BCDB_BCDB_H

#include "memodb/memodb.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Error.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class Module;
class StringRef;
} // end namespace llvm

namespace bcdb {

class BCDB {
  std::unique_ptr<llvm::LLVMContext> Context;
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
  llvm::LLVMContext &GetContext() { return *Context; }

  /// Reset the LLVMContext. This can help reduce memory usage. The caller must
  // guarantee that nothing is using the old context.
  void ResetContext() { Context = std::make_unique<llvm::LLVMContext>(); }

  llvm::Error Delete(llvm::StringRef Name);
  llvm::Expected<std::unique_ptr<llvm::Module>>
  Merge(const std::vector<llvm::StringRef> &Names);
  llvm::Expected<std::unique_ptr<llvm::Module>>
  Mux(std::vector<llvm::StringRef> Names);
  void Mux2(std::vector<llvm::StringRef> Names);

  llvm::Expected<std::unique_ptr<llvm::Module>>
  LoadParts(llvm::StringRef Name, std::map<std::string, std::string> &PartIDs);
};

} // end namespace bcdb

#endif // BCDB_BCDB_H
