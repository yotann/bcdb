#include "bcdb/BCDB.h"

#include "bcdb/AlignBitcode.h"
#include "bcdb/Split.h"

#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <string>
#include <vector>

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
  std::vector<std::string> function_keys;
  std::vector<memodb_value *> function_values;
  memodb_value *remainder_value = nullptr;

  Expected<memodb_value *> SaveModule(Module &M) {
    SmallVector<char, 0> Buffer;
    WriteAlignedModule(M, Buffer);
    memodb_value *value = memodb_blob_create(db, Buffer.data(), Buffer.size());
    return value;
  }

public:
  BCDBSplitSaver(memodb_db *db) : db(db) {}
  ~BCDBSplitSaver() {
    for (memodb_value *value : function_values)
      memodb_value_free(value);
    memodb_value_free(remainder_value);
  }
  Error saveFunction(std::unique_ptr<Module> M, StringRef Name) override {
    Expected<memodb_value *> value = SaveModule(*M);
    if (!value)
      return value.takeError();
    function_keys.push_back(Name);
    function_values.push_back(*value);
    return Error::success();
  }
  Error saveRemainder(std::unique_ptr<Module> M) override {
    Expected<memodb_value *> value = SaveModule(*M);
    if (!value)
      return value.takeError();
    remainder_value = *value;
    return Error::success();
  }
  Expected<memodb_value *> finish() {
    std::vector<const char *> function_keys_c;
    for (const std::string &x : function_keys)
      function_keys_c.push_back(x.c_str());
    memodb_value *function_map =
        memodb_map_create(db, function_keys_c.data(), function_values.data(),
                          function_keys.size());
    if (!function_map)
      return make_error<StringError>("could not create function map",
                                     inconvertibleErrorCode());

    const char *keys[] = {"functions", "remainder"};
    memodb_value *values[] = {function_map, remainder_value};
    memodb_value *result = memodb_map_create(db, keys, values, 2);
    memodb_value_free(function_map);
    if (!result)
      return make_error<StringError>("could not create module map",
                                     inconvertibleErrorCode());
    return result;
  }
};
} // end anonymous namespace

Error BCDB::Add(StringRef Name, std::unique_ptr<Module> M) {
  BCDBSplitSaver Saver(db);
  if (Error Err = SplitModule(std::move(M), Saver))
    return Err;
  Expected<memodb_value *> ValueOrErr = Saver.finish();
  if (!ValueOrErr)
    return ValueOrErr.takeError();
  int rc = memodb_head_set(db, Name.str().c_str(), *ValueOrErr);
  memodb_value_free(*ValueOrErr);
  if (rc)
    return make_error<StringError>("could not update head",
                                   inconvertibleErrorCode());
  return Error::success();
}
