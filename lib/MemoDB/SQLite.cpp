#include "memodb_internal.h"

#include <cassert>
#include <cstring>
#include <sodium.h>
#include <sqlite3.h>

static const char SQLITE_INIT_STMTS[] =
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
class sqlite_db : public memodb_db {
  sqlite3 *db = nullptr;

public:
  int open(const char *path, int create_if_missing);
  memodb_value *blob_create(const void *data, size_t size) override;
  memodb_value *map_create(const char **keys, memodb_value **values,
                           size_t count) override;
  ~sqlite_db() override { sqlite3_close(db); }
};
} // end anonymous namespace

namespace {
struct sqlite_value : public memodb_value {
  sqlite3_int64 id;
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

int sqlite_db::open(const char *path, int create_if_missing) {
  assert(!db);
  int flags =
      SQLITE_OPEN_READWRITE | (create_if_missing ? SQLITE_OPEN_CREATE : 0);
  int rc = sqlite3_open_v2(path, &db, flags, /*zVfs*/ nullptr);
  if (rc != SQLITE_OK)
    return -1;

  rc = sqlite3_exec(db, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr);
  // ignore return code

  rc = sqlite3_exec(db, SQLITE_INIT_STMTS, nullptr, nullptr, nullptr);
  if (rc != SQLITE_OK)
    return -1;
  return 0;
}

memodb_value *sqlite_db::blob_create(const void *data, size_t size) {
  unsigned char hash[crypto_generichash_BYTES];
  crypto_generichash(hash, sizeof hash,
                     reinterpret_cast<const unsigned char *>(data), size,
                     nullptr, 0);

  Transaction transaction(db);

  {
    Stmt select_stmt(db, "SELECT vid FROM blob WHERE hash = ?1");
    select_stmt.bind_blob(1, hash, sizeof hash);
    int step = select_stmt.step();
    if (step == SQLITE_ROW)
      return new sqlite_value(sqlite3_column_int64(select_stmt.stmt, 0));
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
    insert_stmt.bind_blob(3, data, size);
    if (insert_stmt.step() != SQLITE_DONE)
      return nullptr;
  }
  if (transaction.commit() != SQLITE_OK)
    return nullptr;
  return new sqlite_value(result);
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

int memodb_sqlite_open(memodb_db **db_out, const char *path,
                       int create_if_missing) {
  sqlite_db *db = new sqlite_db;
  *db_out = db;
  return db->open(path, create_if_missing);
}
