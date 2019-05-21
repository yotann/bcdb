#include "memodb/memodb.h"

#include "memodb_internal.h"

llvm::Expected<std::unique_ptr<memodb_db>>
memodb_db_open(llvm::StringRef uri, bool create_if_missing) {
  if (uri.startswith("sqlite:")) {
    return memodb_sqlite_open(uri.substr(7), create_if_missing);
  } else {
    return llvm::make_error<llvm::StringError>(
        llvm::Twine("unsupported URI ") + uri, llvm::inconvertibleErrorCode());
  }
}
