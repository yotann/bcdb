#include "memodb/memodb.h"

#include "memodb_internal.h"

#include <cstring>

namespace {
struct error_db : public memodb_db {
  memodb_value *blob_create(const void *data, size_t size) override {
    return nullptr;
  }
};
} // end anonymous namespace

int memodb_db_open(memodb_db **db, const char *uri, int create_if_missing) {
  if (!std::strncmp(uri, "git:", 4)) {
    return memodb_git_open(db, uri + 4, create_if_missing);
  } else if (!std::strncmp(uri, "sqlite:", 7)) {
    return memodb_sqlite_open(db, uri + 7, create_if_missing);
  } else {
    *db = new error_db;
    return -1;
  }
}

void memodb_db_close(memodb_db *db) { delete db; }

void memodb_value_free(memodb_value *value) { delete value; }

memodb_value *memodb_blob_create(memodb_db *db, const void *data, size_t size) {
  return db->blob_create(data, size);
}
