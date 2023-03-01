#ifndef BCDB_BCDB_H
#define BCDB_BCDB_H

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Error.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bcdb/Context.h"

namespace memodb {
class CID;
struct Name;
class Store;
} // end namespace memodb

namespace llvm {
class Module;
class StringRef;
namespace cl {
class OptionCategory;
} // end namespace cl
} // end namespace llvm

namespace bcdb {

extern llvm::cl::OptionCategory BCDBCategory;
extern llvm::cl::OptionCategory MergeCategory;

// LLVM symbol names are usually ASCII, but can contain arbitrary bytes. We
// interpret the bytes as ISO-8859-1 (bytes 0...255 become Unicode codepoints
// 0...255) and convert them to UTF-8 for use in map keys.
std::string bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes);
std::string bytesToUTF8(llvm::StringRef Bytes);
std::string utf8ToByteString(llvm::StringRef Str);

// Join the parts of a module back together and return the result.
llvm::Expected<std::unique_ptr<llvm::Module>>
getSplitModule(llvm::LLVMContext &context, memodb::Store &store,
               const memodb::Name &name);

class BCDB {
  std::unique_ptr<Context> context;
  std::unique_ptr<memodb::Store> unique_db;
  memodb::Store *db;

public:
  BCDB(std::unique_ptr<memodb::Store> db); // freed when BCDB destroyed
  BCDB(memodb::Store &db);                 // not freed when BCDB destroyed
  ~BCDB();
  static llvm::Error Init(llvm::StringRef store_uri);
  static llvm::Expected<std::unique_ptr<BCDB>> Open(llvm::StringRef store_uri);

  memodb::Store &get_db() { return *db; }

  llvm::Expected<memodb::CID> Add(std::unique_ptr<llvm::Module> M);
  llvm::Expected<std::unique_ptr<llvm::Module>>
  GetFunctionById(llvm::StringRef Id);
  llvm::Expected<std::vector<std::string>> ListModules();
  llvm::Expected<std::vector<std::string>>
  ListFunctionsInModule(llvm::StringRef Name);
  llvm::Expected<std::vector<std::string>> ListAllFunctions();
  llvm::LLVMContext &GetContext() { return *context; }

  /// Reset the LLVMContext. This can help reduce memory usage. The caller must
  // guarantee that nothing is using the old context.
  void ResetContext() { context = std::make_unique<Context>(); }

  llvm::Error Delete(llvm::StringRef Name);
  llvm::Expected<std::unique_ptr<llvm::Module>>
  Merge(const std::vector<llvm::StringRef> &Names);
  llvm::Expected<std::unique_ptr<llvm::Module>>
  Mux(std::vector<llvm::StringRef> Names);
  std::unique_ptr<llvm::Module>
  GuidedLinker(std::vector<llvm::StringRef> Names,
               llvm::StringMap<std::unique_ptr<llvm::Module>> &WrapperModules,
               std::unique_ptr<llvm::Module> *WeakModule);

  llvm::Expected<std::unique_ptr<llvm::Module>>
  LoadParts(llvm::StringRef Name, std::map<std::string, std::string> &PartIDs);
};

} // end namespace bcdb

#endif // BCDB_BCDB_H