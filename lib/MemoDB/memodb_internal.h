#ifndef MEMODB_INTERNAL_H_INCLUDED
#define MEMODB_INTERNAL_H_INCLUDED

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

#include "memodb/memodb.h"

std::unique_ptr<memodb_db> memodb_sqlite_open(llvm::StringRef path,
                                              bool create_if_missing);

#endif // MEMODB_INTERNAL_H_INCLUDED
