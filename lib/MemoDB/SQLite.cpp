#include "memodb_internal.h"

#include <llvm/Support/ScopedPrinter.h>

#include <cassert>
#include <cstring>
#include <sodium.h>
#include <sqlite3.h>
#include <vector>

static const char SQLITE_INIT_STMTS[] =
    "CREATE TABLE IF NOT EXISTS head(\n"
    "  hid INTEGER PRIMARY KEY,    -- Head ID\n"
    "  name TEXT UNIQUE NOT NULL,  -- Head name\n"
    "  vid INTEGER                 -- Value ID\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS value(\n"
    "  vid INTEGER PRIMARY KEY,    -- Value ID\n"
    "  type INTEGER NOT NULL       -- Value type\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS blob(\n"
    "  vid INTEGER PRIMARY KEY,    -- Blob ID\n"
    "  hash BLOB UNIQUE NOT NULL,  -- Hash of the content\n"
    "  content BLOB\n"
    ");\n"
    "CREATE TABLE IF NOT EXISTS map(\n"
    "  vid INTEGER,                -- Map ID\n"
    "  key TEXT NOT NULL,          -- Entry key\n"
    "  value INTEGER NOT NULL      -- Entry value\n"
    ");\n"
    "CREATE UNIQUE INDEX IF NOT EXISTS map_index ON map(vid, key);\n";

namespace {
class SQLiteError : public llvm::StringError {
public:
  SQLiteError(sqlite3 *db);
};
} // end anonymous namespace

SQLiteError::SQLiteError(sqlite3 *db)
    : StringError(sqlite3_errmsg(db), llvm::inconvertibleErrorCode()) {}

namespace {
class sqlite_db : public memodb_db {
  sqlite3 *db = nullptr;

public:
  llvm::Error open(const char *path, bool create_if_missing);
  std::string value_get_id(memodb_value *value) override;
  std::unique_ptr<memodb_value>
  blob_create(llvm::ArrayRef<uint8_t> data) override;
  const void *blob_get_buffer(memodb_value *blob) override;
  int blob_get_size(memodb_value *blob, size_t *size) override;
  memodb_value *map_create(const char **keys, memodb_value **values,
                           size_t count) override;
  memodb_value *map_lookup(memodb_value *map, const char *key) override;
  llvm::Expected<std::map<std::string, std::shared_ptr<memodb_value>>>
  map_list_items(memodb_value *map) override;
  llvm::Expected<std::vector<std::string>> list_heads() override;
  memodb_value *head_get(const char *name) override;
  llvm::Error head_set(llvm::StringRef name, memodb_value *value) override;
  ~sqlite_db() override { sqlite3_close(db); }
};
} // end anonymous namespace

namespace {
struct sqlite_value : public memodb_value {
  sqlite3_int64 id;
  std::vector<char> buffer;
  sqlite_value(sqlite3_int64 id) : id(id) {}
};
} // end anonymous namespace

namespace {
struct Stmt {
  sqlite3_stmt *stmt = nullptr;
  int rc;

  Stmt(sqlite3 *db, const char *sql) {
    rc = sqlite3_prepare_v2(db, sql, /*nByte*/ -1, &stmt, nullptr);
  }

  void bind_blob(int i, const void *data, size_t size) {
    if (rc != SQLITE_OK)
      return;
    rc = sqlite3_bind_blob64(stmt, i, data, size, SQLITE_STATIC);
  }

  void bind_int(int i, sqlite3_int64 value) {
    if (rc != SQLITE_OK)
      return;
    rc = sqlite3_bind_int64(stmt, i, value);
  }

  void bind_text(int i, const char *value) {
    if (rc != SQLITE_OK)
      return;
    rc = sqlite3_bind_text(stmt, i, value, -1, SQLITE_STATIC);
  }

  void bind_text(int i, llvm::StringRef value) {
    if (rc != SQLITE_OK)
      return;
    rc = sqlite3_bind_text(stmt, i, value.data(), value.size(), SQLITE_STATIC);
  }

  int step() {
    if (rc != SQLITE_OK)
      return rc;
    return sqlite3_step(stmt);
  }

  void reset() { sqlite3_reset(stmt); }

  ~Stmt() { sqlite3_finalize(stmt); }
};
} // end anonymous namespace

namespace {
class Transaction {
  sqlite3 *db;
  int rc = 0;
  bool committed = false;

public:
  Transaction(sqlite3 *db) : db(db) {
    rc = sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
  }
  int commit() {
    assert(!committed);
    committed = true;
    if (rc)
      return rc;
    return sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
  }
  ~Transaction() {
    if (!committed) {
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
  }
};
} // end anonymous namespace

llvm::Error sqlite_db::open(const char *path, bool create_if_missing) {
  assert(!db);
  int flags =
      SQLITE_OPEN_READWRITE | (create_if_missing ? SQLITE_OPEN_CREATE : 0);
  int rc = sqlite3_open_v2(path, &db, flags, /*zVfs*/ nullptr);
  if (rc != SQLITE_OK)
    return llvm::make_error<SQLiteError>(db);

  rc = sqlite3_exec(db, "PRAGMA journal_mode = WAL; PRAGMA synchronous = 1",
                    nullptr, nullptr, nullptr);
  // ignore return code

  rc = sqlite3_exec(db, SQLITE_INIT_STMTS, nullptr, nullptr, nullptr);
  if (rc != SQLITE_OK)
    return llvm::make_error<SQLiteError>(db);
  return llvm::Error::success();
}

std::string sqlite_db::value_get_id(memodb_value *value) {
  auto id = static_cast<sqlite_value *>(value)->id;
  return llvm::to_string(id);
}

std::unique_ptr<memodb_value>
sqlite_db::blob_create(llvm::ArrayRef<uint8_t> data) {
  unsigned char hash[crypto_generichash_BYTES];
  crypto_generichash(hash, sizeof hash, data.data(), data.size(), nullptr, 0);

  Transaction transaction(db);

  {
    Stmt select_stmt(db, "SELECT vid FROM blob WHERE hash = ?1");
    select_stmt.bind_blob(1, hash, sizeof hash);
    int step = select_stmt.step();
    if (step == SQLITE_ROW)
      return std::make_unique<sqlite_value>(
          sqlite3_column_int64(select_stmt.stmt, 0));
    if (step != SQLITE_DONE)
      return nullptr;
  }

  {
    Stmt insert_stmt(db, "INSERT INTO value(type) VALUES (0)");
    if (insert_stmt.step() != SQLITE_DONE)
      return nullptr;
  }
  sqlite_value result(sqlite3_last_insert_rowid(db));
  {
    Stmt insert_stmt(
        db, "INSERT OR IGNORE INTO blob(vid, hash, content) VALUES (?1,?2,?3)");
    insert_stmt.bind_int(1, result.id);
    insert_stmt.bind_blob(2, hash, sizeof hash);
    insert_stmt.bind_blob(3, data.data(), data.size());
    if (insert_stmt.step() != SQLITE_DONE)
      return nullptr;
  }
  if (transaction.commit() != SQLITE_OK)
    return nullptr;
  return std::make_unique<sqlite_value>(result);
}

const void *sqlite_db::blob_get_buffer(memodb_value *blob) {
  sqlite_value *blob_value = static_cast<sqlite_value *>(blob);
  if (!blob_value->buffer.empty())
    return blob_value->buffer.data();

  Stmt stmt(db, "SELECT content FROM blob WHERE vid = ?1");
  stmt.bind_int(1, blob_value->id);
  if (stmt.step() != SQLITE_ROW)
    return nullptr;
  const char *data =
      reinterpret_cast<const char *>(sqlite3_column_blob(stmt.stmt, 0));
  int size = sqlite3_column_bytes(stmt.stmt, 0);
  blob_value->buffer.assign(data, data + size);
  return blob_value->buffer.data();
}

int sqlite_db::blob_get_size(memodb_value *blob, size_t *size) {
  const sqlite_value *blob_value = static_cast<const sqlite_value *>(blob);
  Stmt stmt(db, "SELECT LENGTH(content) FROM blob WHERE vid = ?1");
  stmt.bind_int(1, blob_value->id);
  if (stmt.step() != SQLITE_ROW)
    return -1;
  *size = sqlite3_column_int64(stmt.stmt, 0);
  return 0;
}

memodb_value *sqlite_db::map_create(const char **keys, memodb_value **values,
                                    size_t count) {
  // TODO: check for identical maps
  Transaction transaction(db);
  {
    Stmt insert_stmt(db, "INSERT INTO value(type) VALUES (1)");
    if (insert_stmt.step() != SQLITE_DONE)
      return nullptr;
  }
  sqlite_value result(sqlite3_last_insert_rowid(db));
  {
    Stmt insert_stmt(db, "INSERT INTO map(vid, key, value) VALUES(?1,?2,?3)");
    insert_stmt.bind_int(1, result.id);
    while (count--) {
      const char *key = *keys++;
      const sqlite_value *value = static_cast<const sqlite_value *>(*values++);
      insert_stmt.bind_text(2, key);
      insert_stmt.bind_int(3, value->id);
      if (insert_stmt.step() != SQLITE_DONE)
        return nullptr;
      insert_stmt.reset();
    }
  }
  if (transaction.commit() != SQLITE_OK)
    return nullptr;
  return new sqlite_value(result);
}

memodb_value *sqlite_db::map_lookup(memodb_value *map, const char *key) {
  const sqlite_value *map_value = static_cast<const sqlite_value *>(map);
  Stmt stmt(db, "SELECT value FROM map WHERE vid = ?1 AND key = ?2");
  stmt.bind_int(1, map_value->id);
  stmt.bind_text(2, key);
  if (stmt.step() != SQLITE_ROW)
    return nullptr;
  return new sqlite_value(sqlite3_column_int64(stmt.stmt, 0));
}

llvm::Expected<std::map<std::string, std::shared_ptr<memodb_value>>>
sqlite_db::map_list_items(memodb_value *map) {
  const sqlite_value *map_value = static_cast<const sqlite_value *>(map);
  Stmt stmt(db, "SELECT key, value FROM map WHERE vid = ?1");
  stmt.bind_int(1, map_value->id);
  std::map<std::string, std::shared_ptr<memodb_value>> result;
  while (true) {
    auto rc = stmt.step();
    if (rc == SQLITE_DONE)
      break;
    if (rc != SQLITE_ROW)
      return llvm::make_error<SQLiteError>(db);
    std::string key(
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.stmt, 0)));
    auto value =
        std::make_shared<sqlite_value>(sqlite3_column_int64(stmt.stmt, 1));
    result[std::move(key)] = std::move(value);
  }
  return result;
}

llvm::Expected<std::vector<std::string>> sqlite_db::list_heads() {
  std::vector<std::string> result;
  Stmt stmt(db, "SELECT name FROM head");
  while (true) {
    auto rc = stmt.step();
    if (rc == SQLITE_DONE)
      break;
    if (rc != SQLITE_ROW)
      return llvm::make_error<SQLiteError>(db);
    result.emplace_back(
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.stmt, 0)));
  }
  return result;
}

memodb_value *sqlite_db::head_get(const char *name) {
  Stmt stmt(db, "SELECT vid FROM head WHERE name = ?1");
  stmt.bind_text(1, name);
  if (stmt.step() != SQLITE_ROW)
    return nullptr;
  return new sqlite_value(sqlite3_column_int64(stmt.stmt, 0));
}

llvm::Error sqlite_db::head_set(llvm::StringRef name, memodb_value *value) {
  auto v = static_cast<const sqlite_value *>(value);
  Stmt insert_stmt(db, "INSERT OR REPLACE INTO head(name, vid) VALUES(?1,?2)");
  insert_stmt.bind_text(1, name);
  insert_stmt.bind_int(2, v->id);
  if (insert_stmt.step() != SQLITE_DONE)
    return llvm::make_error<SQLiteError>(db);
  return llvm::Error::success();
}

llvm::Expected<std::unique_ptr<memodb_db>>
memodb_sqlite_open(llvm::StringRef path, bool create_if_missing) {
  auto db = std::make_unique<sqlite_db>();
  llvm::Error error = db->open(path.str().c_str(), create_if_missing);
  if (error)
    return std::move(error);
  return std::move(db);
}
