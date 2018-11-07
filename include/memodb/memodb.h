#ifndef MEMODB_MEMODB_H
#define MEMODB_MEMODB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct memodb_db memodb_db;
typedef struct memodb_value memodb_value;

int memodb_db_open(memodb_db **db, const char *uri, int create_if_missing);
void memodb_db_close(memodb_db *db);

void memodb_value_free(memodb_value *value);

memodb_value *memodb_blob_create(memodb_db *db, const void *data, size_t size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MEMODB_MEMODB_H
