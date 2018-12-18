#include "memodb_internal.h"

#include <cassert>
#include <cstring>
#include <git2.h>
#include <string>

namespace {
class git_db : public memodb_db {
  git_repository *repo = nullptr;

public:
  int open(const char *path, int create_if_missing);
  memodb_value *blob_create(const void *data, size_t size) override;
  const void *blob_get_buffer(memodb_value *blob) override;
  int blob_get_size(memodb_value *blob, size_t *size) override;
  memodb_value *map_create(const char **keys, memodb_value **values,
                           size_t count) override;
  memodb_value *map_lookup(memodb_value *map, const char *key) override;
  memodb_value *head_get(const char *name) override;
  int head_set(const char *name, memodb_value *value) override;
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

const void *git_db::blob_get_buffer(memodb_value *blob) {
  return nullptr; // FIXME
}

int git_db::blob_get_size(memodb_value *blob, size_t *size) {
  return -1; // FIXME
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

memodb_value *git_db::map_lookup(memodb_value *map, const char *key) {
  return nullptr; // FIXME
}

memodb_value *git_db::head_get(const char *name) {
  git_value result;
  result.is_dir = true;
  int rc = git_reference_name_to_id(&result.id, repo, name);
  if (rc)
    return nullptr;
  return new git_value(result);
}

int git_db::head_set(const char *name, memodb_value *value) {
  git_signature *signature;
  int rc = git_signature_now(&signature, "MemoDB", "memodb");
  if (rc)
    return -1;

  auto tree_value = static_cast<const git_value *>(value);

  git_tree *tree;
  rc = git_tree_lookup(&tree, repo, &tree_value->id);
  if (rc) {
    git_signature_free(signature);
    return -1;
  }

  git_oid commit_id;
  rc =
      git_commit_create(&commit_id, repo, /*update_ref*/ nullptr, signature,
                        signature, /*message_encoding*/ nullptr, /*message*/ "",
                        tree, /*parent_count*/ 0, /*parents*/ nullptr);
  git_signature_free(signature);
  git_tree_free(tree);
  if (rc)
    return -1;

  // TODO: escape ref_name
  std::string ref_name = std::string("refs/heads/") + name;
  rc = git_reference_create(/*out*/ nullptr, repo, ref_name.c_str(), &commit_id,
                            /*force*/ 1, /*log_message*/ "updated");
  if (rc)
    return -1;

  return 0;
}

int memodb_git_open(memodb_db **db_out, const char *path,
                    int create_if_missing) {
  git_db *db = new git_db;
  *db_out = db;
  return db->open(path, create_if_missing);
}
