#include "memodb_internal.h"

#include <cassert>
#include <cstring>
#include <git2.h>

namespace {
class git_db : public memodb_db {
  git_repository *repo = nullptr;

public:
  int open(const char *path, int create_if_missing);
  memodb_value *blob_create(const void *data, size_t size) override;
  memodb_value *map_create(const char **keys, memodb_value **values,
                           size_t count) override;
  ~git_db() override {
    git_repository_free(repo);
    git_libgit2_shutdown();
  }
};
} // end anonymous namespace

namespace {
struct git_value : public memodb_value {
  git_oid id;
  bool is_dir;
};
} // end anonymous namespace

namespace {
struct TreeBuilder {
  git_treebuilder *builder = nullptr;
  ~TreeBuilder() { git_treebuilder_free(builder); }
};
} // end anonymous namespace

int git_db::open(const char *path, int create_if_missing) {
  assert(!repo);
  git_libgit2_init();
  int rc = git_repository_open_bare(&repo, path);
  if (rc == GIT_ENOTFOUND && create_if_missing)
    rc = git_repository_init(&repo, path, /*is_bare*/ 1);
  if (rc)
    return -1;
  return 0;
}

memodb_value *git_db::blob_create(const void *data, size_t size) {
  git_value value;
  value.is_dir = false;
  int rc = git_blob_create_frombuffer(&value.id, repo, data, size);
  if (rc)
    return nullptr;
  return new git_value(value);
}

memodb_value *git_db::map_create(const char **keys, memodb_value **values,
                                 size_t count) {
  TreeBuilder builder;
  int rc = git_treebuilder_new(&builder.builder, repo, /*source*/ nullptr);
  if (rc)
    return nullptr;

  while (count--) {
    // TODO: escape filenames
    const char *key = *keys++;
    const git_value *value = static_cast<const git_value *>(*values++);
    rc = git_treebuilder_insert(
        /*out*/ nullptr, builder.builder, key, &value->id,
        value->is_dir ? GIT_FILEMODE_TREE : GIT_FILEMODE_BLOB);
    if (rc)
      return nullptr;
  }

  git_value result;
  result.is_dir = true;
  rc = git_treebuilder_write(&result.id, builder.builder);
  if (rc)
    return nullptr;
  return new git_value(result);
}

int memodb_git_open(memodb_db **db_out, const char *path,
                    int create_if_missing) {
  git_db *db = new git_db;
  *db_out = db;
  return db->open(path, create_if_missing);
}
