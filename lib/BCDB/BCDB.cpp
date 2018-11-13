#include "bcdb/BCDB.h"

#include "bcdb/AlignBitcode.h"
#include "bcdb/Split.h"

#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/Errc.h>

using namespace bcdb;
using namespace llvm;

Error BCDB::Init(StringRef uri) {
  memodb_db *db;
  int rc = memodb_db_open(&db, uri.str().c_str(), /*create_if_missing*/ 1);
  memodb_db_close(db);
  if (rc)
    return make_error<StringError>("could not open new database",
                                   inconvertibleErrorCode());
  return Error::success();
}

Expected<std::unique_ptr<BCDB>> BCDB::Open(StringRef uri) {
  memodb_db *db;
  int rc = memodb_db_open(&db, uri.str().c_str(), /*create_if_missing*/ 0);
  if (rc) {
    memodb_db_close(db);
    return make_error<StringError>("could not open database",
                                   inconvertibleErrorCode());
  }
  return std::unique_ptr<BCDB>(new BCDB(db));
}

BCDB::BCDB(memodb_db *db) : db(db) {}

BCDB::~BCDB() { memodb_db_close(db); }

namespace {
class BCDBSplitSaver : public SplitSaver {
  memodb_db *db;
  void SaveModule(Module &M) {
    SmallVector<char, 0> Buffer;
    WriteAlignedModule(M, Buffer);
    memodb_value *value = memodb_blob_create(db, Buffer.data(), Buffer.size());
    memodb_value_free(value);
  }

public:
  BCDBSplitSaver(memodb_db *db) : db(db) {}
  void saveFunction(std::unique_ptr<Module> M, StringRef Name) override {
    SaveModule(*M);
  }
  void saveRemainder(std::unique_ptr<Module> M) override { SaveModule(*M); }
};
} // end anonymous namespace

Error BCDB::Add(std::unique_ptr<Module> M) {
  BCDBSplitSaver Saver(db);
  SplitModule(std::move(M), Saver);
  return Error::success();
}
