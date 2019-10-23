#ifndef BCDB_WHOLEPROGRAM_H
#define BCDB_WHOLEPROGRAM_H

namespace llvm {
class Module;
namespace object {
class Binary;
} // end namespace object
} // end namespace llvm

namespace bcdb {

bool ImitateBinary(llvm::Module &M, llvm::object::Binary &B);

} // end namespace bcdb

#endif // BCDB_WHOLEPROGRAM_H
