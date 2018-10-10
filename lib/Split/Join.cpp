#include "bcdb/Split.h"

#include <llvm/IR/Module.h>

#include <memory>

using namespace bcdb;
using namespace llvm;

std::unique_ptr<Module> bcdb::JoinModule(SplitLoader &Loader) {
  return Loader.loadRemainder();
}
