#ifndef MEMODB_INTERNAL_H_INCLUDED
#define MEMODB_INTERNAL_H_INCLUDED

#include <cstddef>

class memodb_value {
public:
  virtual ~memodb_value() {}
};

class memodb_db {
public:
  virtual memodb_value *blob_create(const void *data, size_t size) = 0;
  virtual memodb_value *map_create(const char **keys, memodb_value **values,
                                   size_t count) = 0;
  virtual ~memodb_db() {}
};

int memodb_git_open(memodb_db **db, const char *path, int create_if_missing);
int memodb_sqlite_open(memodb_db **db, const char *path, int create_if_missing);

#endif // MEMODB_INTERNAL_H_INCLUDED
