#ifndef MEMODB_MEMODB_H
#define MEMODB_MEMODB_H

#include <map>
#include <memory>
#include <stddef.h>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>

class memodb_value {
public:
  virtual ~memodb_value() {}
};

class memodb_db {
public:
  virtual std::string value_get_id(memodb_value *value) = 0;
  virtual std::unique_ptr<memodb_value> get_value_by_id(llvm::StringRef id) = 0;
  virtual std::unique_ptr<memodb_value>
  blob_create(llvm::ArrayRef<uint8_t> data) = 0;
  virtual const void *blob_get_buffer(memodb_value *blob) = 0;
  virtual int blob_get_size(memodb_value *blob, size_t *size) = 0;
  virtual memodb_value *map_create(const char **keys, memodb_value **values,
                                   size_t count) = 0;
  virtual memodb_value *map_lookup(memodb_value *map, const char *key) = 0;
  virtual std::map<std::string, std::shared_ptr<memodb_value>>
  map_list_items(memodb_value *map) = 0;
  virtual std::vector<std::string> list_heads() = 0;
  virtual memodb_value *head_get(const char *name) = 0;
  virtual void head_set(llvm::StringRef name, memodb_value *value) = 0;
  virtual ~memodb_db() {}
  virtual void head_delete(llvm::StringRef name);
};

std::unique_ptr<memodb_db> memodb_db_open(llvm::StringRef uri,
                                          bool create_if_missing = false);

#endif // MEMODB_MEMODB_H
