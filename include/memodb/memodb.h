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
const void *memodb_blob_get_buffer(memodb_db *db, memodb_value *blob);
int memodb_blob_get_size(memodb_db *db, memodb_value *blob, size_t *size);

memodb_value *memodb_map_create(memodb_db *db, const char **keys,
                                memodb_value **values, size_t count);
memodb_value *memodb_map_lookup(memodb_db *db, memodb_value *map,
                                const char *key);

memodb_value *memodb_head_get(memodb_db *db, const char *name);
int memodb_head_set(memodb_db *db, const char *name, memodb_value *value);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MEMODB_MEMODB_H
