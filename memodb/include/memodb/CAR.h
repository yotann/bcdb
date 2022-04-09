#ifndef MEMODB_CAR_H
#define MEMODB_CAR_H

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/raw_ostream.h>

#include "Store.h"

namespace memodb {

CID exportToCARFile(llvm::raw_fd_ostream &os, Store &store,
                    llvm::ArrayRef<Name> names_to_export = {});

}

#endif // MEMODB_CAR_H
