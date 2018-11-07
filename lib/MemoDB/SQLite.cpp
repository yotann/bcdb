#include "memodb_internal.h"

#include <cassert>
#include <cstring>
#include <sodium.h>
#include <sqlite3.h>

static const char SQLITE_INIT_STMTS[] =
    "CREATE TABLE IF NOT EXISTS blob(\n"
    "  bid INTEGER PRIMARY KEY,    -- Blob ID\n"
    "  hash BLOB UNIQUE NOT NULL,  -- Hash of the content\n"
    "  content BLOB\n"
    ");";

namespace {
class sqlite_db : public memodb_db {
  sqlite3 *db = nullptr;

public:
  int open(const char *path, int create_if_missing);
  memodb_value *blob_create(const void *data, size_t size) override;
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

  int step() {
    if (rc != SQLITE_OK)
      return rc;
    return sqlite3_step(stmt);
  }

  ~Stmt() { sqlite3_finalize(stmt); }
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

  {
    Stmt insert_stmt(
        db, "INSERT OR IGNORE INTO blob(hash, content) VALUES (?1,?2)");
    insert_stmt.bind_blob(1, hash, sizeof hash);
    insert_stmt.bind_blob(2, data, size);
    if (insert_stmt.step() != SQLITE_DONE)
      return nullptr;
  }

  {
    Stmt select_stmt(db, "SELECT bid FROM blob WHERE hash = ?1");
    select_stmt.bind_blob(1, hash, sizeof hash);
    if (select_stmt.step() != SQLITE_ROW)
      return nullptr;
    return new sqlite_value(sqlite3_column_int64(select_stmt.stmt, 0));
  }
}

int memodb_sqlite_open(memodb_db **db_out, const char *path,
                       int create_if_missing) {
  sqlite_db *db = new sqlite_db;
  *db_out = db;
  return db->open(path, create_if_missing);
}
