#ifndef BCDB_SPLIT_H
#define BCDB_SPLIT_H

#include <llvm/Support/Error.h>
#include <memory>

namespace llvm {
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

llvm::Expected<std::unique_ptr<llvm::Module>> JoinModule(SplitLoader &Loader);
llvm::Error SplitModule(std::unique_ptr<llvm::Module> M, SplitSaver &Saver);

} // end namespace bcdb

#endif // BCDB_ALIGNBITCODE_H
