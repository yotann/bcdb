#ifndef MEMODB_INTERNAL_H_INCLUDED
#define MEMODB_INTERNAL_H_INCLUDED

#include <llvm/ADT/StringRef.h>
#include <memory>

namespace memodb {

class Evaluator;
class Store;

std::unique_ptr<Evaluator> createClientEvaluator(llvm::StringRef path,
                                                 unsigned num_threads);

}; // namespace memodb

std::unique_ptr<memodb::Store> memodb_car_open(llvm::StringRef path,
                                               bool create_if_missing);

std::unique_ptr<memodb::Store> memodb_http_open(llvm::StringRef path,
                                                bool create_if_missing);

std::unique_ptr<memodb::Store> memodb_rocksdb_open(llvm::StringRef path,
                                                   bool create_if_missing);

std::unique_ptr<memodb::Store> memodb_sqlite_open(llvm::StringRef path,
                                                  bool create_if_missing);

#endif // MEMODB_INTERNAL_H_INCLUDED
