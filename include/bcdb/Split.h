#ifndef BCDB_SPLIT_H
#define BCDB_SPLIT_H

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <memory>

namespace bcdb {

class SplitSaver {
public:
  virtual void saveFunction(llvm::StringRef Name,
                            std::unique_ptr<llvm::Module> Module) = 0;
  virtual void saveRemainder(std::unique_ptr<llvm::Module> Module) = 0;
};

void SplitModule(std::unique_ptr<llvm::Module> M, SplitSaver &Saver);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
