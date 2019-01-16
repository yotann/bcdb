#include "memodb/memodb.h"

#include "memodb_internal.h"

#include <cstring>

namespace {
struct error_db : public memodb_db {
  memodb_value *blob_create(const void *data, size_t size) override {
    return nullptr;
  }
  memodb_value *map_create(const char **keys, memodb_value **values,
                           size_t count) override {
    return nullptr;
  }
  const void *blob_get_buffer(memodb_value *blob) override { return nullptr; }
  int blob_get_size(memodb_value *blob, size_t *size) override { return -1; }
  memodb_value *map_lookup(memodb_value *map, const char *key) override {
    return nullptr;
  }
  memodb_value *head_get(const char *name) override { return nullptr; }
  int head_set(const char *name, memodb_value *value) override { return -1; }
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

const void *memodb_blob_get_buffer(memodb_db *db, memodb_value *blob) {
  return db->blob_get_buffer(blob);
}

int memodb_blob_get_size(memodb_db *db, memodb_value *blob, size_t *size) {
  return db->blob_get_size(blob, size);
}

memodb_value *memodb_map_create(memodb_db *db, const char **keys,
                                memodb_value **values, size_t count) {
  return db->map_create(keys, values, count);
}

memodb_value *memodb_map_lookup(memodb_db *db, memodb_value *map,
                                const char *key) {
  return db->map_lookup(map, key);
}

memodb_value *memodb_head_get(memodb_db *db, const char *name) {
  return db->head_get(name);
}

int memodb_head_set(memodb_db *db, const char *name, memodb_value *value) {
  return db->head_set(name, value);
}
