#ifndef BCDB_SPLIT_H
#define BCDB_SPLIT_H

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <memory>

namespace bcdb {

class SplitLoader {
public:
  virtual std::unique_ptr<llvm::Module> loadFunction(llvm::StringRef Name) = 0;
  virtual std::unique_ptr<llvm::Module> loadRemainder() = 0;
};

class SplitSaver {
public:
  virtual void saveFunction(std::unique_ptr<llvm::Module> Module,
                            llvm::StringRef Name) = 0;
  virtual void saveRemainder(std::unique_ptr<llvm::Module> Module) = 0;
};

std::unique_ptr<llvm::Module> JoinModule(SplitLoader &Loader);
void SplitModule(std::unique_ptr<llvm::Module> M, SplitSaver &Saver);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
