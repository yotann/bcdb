#ifndef BCDB_BCDB_H
#define BCDB_BCDB_H

#include "memodb/memodb.h"

#include <llvm/Support/Error.h>
#include <memory>

namespace llvm {
class Module;
class StringRef;
} // end namespace llvm

namespace bcdb {

class BCDB {
  memodb_db *db;
  BCDB(memodb_db *db);

public:
  static llvm::Error Init(llvm::StringRef uri);
  static llvm::Expected<std::unique_ptr<BCDB>> Open(llvm::StringRef uri);

  ~BCDB();
  llvm::Error Add(llvm::StringRef Name, std::unique_ptr<llvm::Module> M);
};

} // end namespace bcdb

#endif // BCDB_BCDB_H
