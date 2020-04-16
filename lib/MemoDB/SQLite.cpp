#include "memodb_internal.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/ScopedPrinter.h>

#include <cassert>
#include <cstring>
#include <sodium.h>
#include <sqlite3.h>
#include <vector>

enum ValueType {
  BYTES_BLOB = 0,   // bytestring stored directly in "blob" table
  OBSOLETE_MAP = 1, // old map format stored in "map" table
  CBOR_BLOB = 2,    // arbitrary value stored as CBOR in "blob" table
};

// Errors when running these statements are ignored.
static const std::vector<const char *> SQLITE_PRAGMAS = {
  "PRAGMA busy_timeout = 10000;\n",
  "PRAGMA foreign_keys = ON;\n",
  "PRAGMA journal_mode = WAL;\n",
  "PRAGMA synchronous = 1;\n",
};

const unsigned int CURRENT_VERSION = 4;
const unsigned long APPLICATION_ID = 1111704642;

static const char SQLITE_INIT_STMTS[] =
    "PRAGMA user_version = 4;\n"
    "PRAGMA application_id = 1111704642;\n"
    "CREATE TABLE value(\n"
    "  vid     INTEGER PRIMARY KEY,\n"
    "  type    INTEGER NOT NULL\n"
    ");\n"
    "CREATE TABLE head(\n"
    "  hid     INTEGER PRIMARY KEY,\n"
    "  name    TEXT    NOT NULL UNIQUE,\n"
    "  vid     INTEGER NOT NULL REFERENCES value(vid)\n"
    ");\n"
    "CREATE INDEX head_by_vid ON head(vid);\n"
    "CREATE TABLE refs(\n"
    "  src     INTEGER NOT NULL REFERENCES value(vid),\n"
    "  dest    INTEGER NOT NULL REFERENCES value(vid),\n"
    "  UNIQUE(dest, src)\n"
    ");\n"
    "CREATE TABLE blob(\n"
    "  vid     INTEGER PRIMARY KEY REFERENCES value(vid),\n"
    "  type    INTEGER NOT NULL,\n"
    "  hash    BLOB    NOT NULL,\n"
    "          -- Hash of the content\n"
    "  content BLOB    NOT NULL,\n"
    "  UNIQUE(hash, type)\n"
    ");\n"
    "CREATE TABLE func(\n"
    "  fid     INTEGER PRIMARY KEY,\n"
    "  name    TEXT    NOT NULL UNIQUE\n"
    ");\n"
    "CREATE TABLE call(\n"
    "  cid     INTEGER PRIMARY KEY,\n"
    "  fid     INTEGER NOT NULL REFERENCES func(fid),\n"
    "  parent  INTEGER          REFERENCES call(cid),\n"
    "          -- (only if this is not the first arg)\n"
    "  arg     INTEGER NOT NULL REFERENCES value(vid),\n"
    "  result  INTEGER          REFERENCES value(vid)\n"
    "          -- (only if this is the last arg)\n"
    ");\n"
    "CREATE INDEX call_by_arg ON call(arg, fid, parent);\n"
    "CREATE INDEX call_by_result ON call(result, fid);\n";

namespace {
class sqlite_db : public memodb_db {
  sqlite3 *db = nullptr;

  void fatal_error();

  memodb_ref id_to_ref(sqlite3_int64 id);
  sqlite3_int64 ref_to_id(const memodb_ref &ref);
  sqlite3_int64 get_fid(llvm::StringRef name, bool create_if_missing = false);

  memodb_value get_obsolete(const memodb_ref &ref, bool binary_keys = false);

  void add_refs_from(sqlite3_int64 id, const memodb_value &value);

  void upgrade_schema();

public:
  void open(const char *path, bool create_if_missing);
  ~sqlite_db() override;

  memodb_value get(const memodb_ref &ref) override;
  memodb_ref put(const memodb_value &value) override;
  std::vector<memodb_ref> list_refs_using(const memodb_ref &ref) override;

  std::vector<std::string> list_heads() override;
  std::vector<std::string> list_heads_using(const memodb_ref &ref) override;
  memodb_ref head_get(llvm::StringRef name) override;
  void head_set(llvm::StringRef name, const memodb_ref &ref) override;
  void head_delete(llvm::StringRef name) override;

  memodb_ref call_get(llvm::StringRef name,
                      llvm::ArrayRef<memodb_ref> args) override;
  void call_set(llvm::StringRef name, llvm::ArrayRef<memodb_ref> args,
                const memodb_ref &result) override;
  void call_invalidate(llvm::StringRef name) override;
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
  Transaction(sqlite3 *db, bool exclusive = false) : db(db) {
    rc = sqlite3_exec(db, exclusive ? "BEGIN EXCLUSIVE" : "BEGIN", nullptr, nullptr, nullptr);
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

void sqlite_db::fatal_error() { llvm::report_fatal_error(sqlite3_errmsg(db)); }

void sqlite_db::open(const char *path, bool create_if_missing) {
  assert(!db);
  int flags =
      SQLITE_OPEN_READWRITE | (create_if_missing ? SQLITE_OPEN_CREATE : 0);
  int rc = sqlite3_open_v2(path, &db, flags, /*zVfs*/ nullptr);
  if (rc != SQLITE_OK)
    fatal_error();

  for (const char *stmt : SQLITE_PRAGMAS) {
    rc = sqlite3_exec(db, stmt, nullptr, nullptr, nullptr);
    // ignore return code
  }

  upgrade_schema();
}

sqlite_db::~sqlite_db() {
  sqlite3_exec(db, "PRAGMA optimize;", nullptr, nullptr, nullptr);
  // ignore return code

  sqlite3_close(db);
}

void sqlite_db::upgrade_schema() {
  int rc;

  // Exit early if the schema is already current.
  sqlite3_int64 user_version;
  {
    Stmt stmt(db, "PRAGMA user_version");
    if (stmt.step() != SQLITE_ROW)
      fatal_error();
    user_version = sqlite3_column_int64(stmt.stmt, 0);
  }
  if (user_version == CURRENT_VERSION)
    return;

  // Start an exclusive transaction so the upgrade process doesn't conflict
  // with other processes.
  Transaction transaction(db, true);

  // If the database is empty, initialize it.
  {
    Stmt exists_stmt(
        db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='value'");
    if (exists_stmt.step() == SQLITE_DONE) {
      rc = sqlite3_exec(db, SQLITE_INIT_STMTS, nullptr, nullptr, nullptr);
      if (rc != SQLITE_OK)
        fatal_error();
    }
  }

  {
    Stmt stmt(db, "PRAGMA user_version");
    if (stmt.step() != SQLITE_ROW)
      fatal_error();
    user_version = sqlite3_column_int64(stmt.stmt, 0);
  }

  if (user_version > CURRENT_VERSION) {
    llvm::errs() << "The BCDB format is too new (this BCDB file uses format "
                 << user_version << ", but we only support format "
                 << CURRENT_VERSION << ")\n";
    llvm::errs() << "Please upgrade your BCDB software!\n";
    fatal_error();
  }

  if (user_version < CURRENT_VERSION)
    llvm::errs() << "Upgrading BCDB format from " << user_version << " to "
                 << CURRENT_VERSION << "...\n";

  // Version 1 stores values as CBOR instead of using the map table.
  if (user_version < 1) {
    static const char UPGRADE_STMTS[] =
        "ALTER TABLE blob ADD COLUMN type INTEGER;\n"
        "UPDATE blob SET type = 0;\n"
        "CREATE UNIQUE INDEX blob_unique_hash ON blob(hash, type);\n"
        "CREATE TABLE IF NOT EXISTS func(\n"
        "  fid     INTEGER PRIMARY KEY,\n"
        "  name    TEXT    NOT NULL UNIQUE\n"
        ");\n"
        "CREATE TABLE IF NOT EXISTS call(\n"
        "  cid     INTEGER PRIMARY KEY,\n"
        "  fid     INTEGER NOT NULL REFERENCES func(fid),\n"
        "  parent  INTEGER          REFERENCES call(cid),\n"
        "          -- (only if this is not the first arg)\n"
        "  arg     INTEGER NOT NULL REFERENCES value(vid),\n"
        "  result  INTEGER          REFERENCES value(vid)\n"
        "          -- (only if this is the last arg)\n"
        ");\n";
    rc = sqlite3_exec(db, UPGRADE_STMTS, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
      fatal_error();
  }

  // Version 3 is the same as version 2, but forces old-format maps to be
  // converted to CBOR. Note that we do this upgrade *before* the version 2
  // upgrade, which needs to load values in CBOR format.
  if (user_version < 3) {
    Stmt stmt(db, "SELECT vid FROM value WHERE type = ?1");
    stmt.bind_int(1, OBSOLETE_MAP);
    while (true) {
      rc = stmt.step();
      if (rc == SQLITE_DONE)
        break;
      else if (rc != SQLITE_ROW)
        fatal_error();

      sqlite3_int64 id = sqlite3_column_int64(stmt.stmt, 0);
      memodb_value value = get_obsolete(id_to_ref(id));

      // Ensure each value is unique. Otherwise, we would need to deduplicate
      // the new values and change their ID numbers, which is too much work.
      value["obsolete_vid"] = id;

      std::vector<std::uint8_t> buffer;
      value.save_cbor(buffer);
      unsigned char hash[crypto_generichash_BYTES];
      crypto_generichash(hash, sizeof hash, buffer.data(), buffer.size(),
                         nullptr, 0);

      Stmt stmt(db,
                "INSERT OR IGNORE INTO blob(vid, type, hash, content) VALUES "
                "(?1,?2,?3,?4)");
      stmt.bind_int(1, id);
      stmt.bind_int(2, CBOR_BLOB);
      stmt.bind_blob(3, hash, sizeof hash);
      stmt.bind_blob(4, buffer.data(), buffer.size());
      if (stmt.step() != SQLITE_DONE)
        fatal_error();
    }

    static const char UPGRADE_STMTS[] =
        "UPDATE value SET type = 2 WHERE type = 1;\n"
        "DROP TABLE IF EXISTS map;\n";
    rc = sqlite3_exec(db, UPGRADE_STMTS, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
      fatal_error();
  }

  // Version 2 adds the refs table.
  if (user_version < 2) {
    static const char UPGRADE_STMTS[] =
        "CREATE TABLE IF NOT EXISTS refs(\n"
        "  src     INTEGER NOT NULL REFERENCES value(vid),\n"
        "  dest    INTEGER NOT NULL REFERENCES value(vid),\n"
        "  UNIQUE(dest, src)\n"
        ");\n";
    rc = sqlite3_exec(db, UPGRADE_STMTS, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
      fatal_error();

    Stmt stmt(db, "SELECT vid FROM value WHERE type != ?1");
    stmt.bind_int(1, BYTES_BLOB);
    while (true) {
      rc = stmt.step();
      if (rc == SQLITE_DONE)
        break;
      else if (rc != SQLITE_ROW)
        fatal_error();
      sqlite3_int64 id = sqlite3_column_int64(stmt.stmt, 0);
      memodb_value value = get(id_to_ref(id));
      add_refs_from(id, value);
    }
  }

  // Version 4 adds some indexes and the application_id.
  if (user_version < 4) {
    static const char UPGRADE_STMTS[] =
        "CREATE INDEX IF NOT EXISTS head_by_vid ON head(vid);\n"
        "DROP INDEX IF EXISTS call_by_arg;\n"
        "CREATE INDEX call_by_arg ON call(arg, fid, parent);\n"
        "CREATE INDEX IF NOT EXISTS call_by_result ON call(result, fid);\n"
        "PRAGMA application_id = 1111704642;\n"
        "PRAGMA user_version = 4;\n";
    rc = sqlite3_exec(db, UPGRADE_STMTS, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
      fatal_error();
  }

  // NOTE: it might be nice to run VACUUM here. However, it can be extremely
  // slow and it requires either gigabytes of RAM or gigabytes of /tmp space
  // (depending on the value of PRAGMA temp_store).

  if (transaction.commit() != SQLITE_OK)
    fatal_error();

  // Ensure the new user_version/application_id are written to the actual
  // database file.
  rc = sqlite3_exec(db, "PRAGMA wal_checkpoint(FULL);", nullptr, nullptr, nullptr);
  // ignore return value
}

memodb_ref sqlite_db::id_to_ref(sqlite3_int64 id) {
  return memodb_ref(llvm::to_string(id));
}

sqlite3_int64 sqlite_db::ref_to_id(const memodb_ref &ref) {
  sqlite3_int64 id;
  if (llvm::StringRef(ref).getAsInteger(10, id))
    fatal_error();
  return id;
}

memodb_ref sqlite_db::put(const memodb_value &value) {
  std::vector<std::uint8_t> buffer;
  int value_type;
  if (value.type() == memodb_value::BYTES) {
    value_type = BYTES_BLOB;
    buffer = value.as_bytes();
  } else {
    value_type = CBOR_BLOB;
    value.save_cbor(buffer);
  }

  unsigned char hash[crypto_generichash_BYTES];
  crypto_generichash(hash, sizeof hash, buffer.data(), buffer.size(), nullptr,
                     0);
  Transaction transaction(db);

  // Check for an existing entry.
  {
    Stmt select_stmt(db, "SELECT vid FROM blob WHERE hash = ?1 AND type = ?2");
    select_stmt.bind_blob(1, hash, sizeof hash);
    select_stmt.bind_int(2, value_type);
    int step = select_stmt.step();
    if (step == SQLITE_ROW)
      return id_to_ref(sqlite3_column_int64(select_stmt.stmt, 0));
    if (step != SQLITE_DONE)
      fatal_error();
  }

  // Add the new entry to the value table.
  sqlite3_int64 new_id;
  {
    Stmt stmt(db, "INSERT INTO value(type) VALUES (?1)");
    stmt.bind_int(1, value_type);
    if (stmt.step() != SQLITE_DONE)
      fatal_error();
    new_id = sqlite3_last_insert_rowid(db);
  }

  // Add the new entry to the blob table.
  {
    Stmt stmt(db, "INSERT OR IGNORE INTO blob(vid, type, hash, content) VALUES "
                  "(?1,?2,?3,?4)");
    stmt.bind_int(1, new_id);
    stmt.bind_int(2, value_type);
    stmt.bind_blob(3, hash, sizeof hash);
    stmt.bind_blob(4, buffer.data(), buffer.size());
    if (stmt.step() != SQLITE_DONE)
      fatal_error();
  }

  // Update the refs table.
  add_refs_from(new_id, value);

  if (transaction.commit() != SQLITE_OK)
    fatal_error();
  return id_to_ref(new_id);
}

void sqlite_db::add_refs_from(sqlite3_int64 id, const memodb_value &value) {
  if (value.type() == memodb_value::REF) {
    auto dest = ref_to_id(value.as_ref());
    Stmt stmt(db, "INSERT OR IGNORE INTO refs(src, dest) VALUES (?1,?2)");
    stmt.bind_int(1, id);
    stmt.bind_int(2, dest);
    if (stmt.step() != SQLITE_DONE)
      fatal_error();
  } else if (value.type() == memodb_value::ARRAY) {
    for (const memodb_value &item : value.array_items())
      add_refs_from(id, item);
  } else if (value.type() == memodb_value::MAP) {
    for (const auto &item : value.map_items()) {
      add_refs_from(id, item.first);
      add_refs_from(id, item.second);
    }
  }
}

memodb_value sqlite_db::get(const memodb_ref &ref) {
  Stmt stmt(db, "SELECT content, type FROM blob WHERE vid = ?1");
  stmt.bind_int(1, ref_to_id(ref));
  int rc = stmt.step();
  if (rc != SQLITE_ROW)
    fatal_error();

  int value_type = sqlite3_column_int64(stmt.stmt, 1);
  const char *data =
      reinterpret_cast<const char *>(sqlite3_column_blob(stmt.stmt, 0));
  int size = sqlite3_column_bytes(stmt.stmt, 0);
  switch (value_type) {
  case BYTES_BLOB:
    return memodb_value::bytes(llvm::StringRef(data, size));
  case CBOR_BLOB:
    return memodb_value::load_cbor(llvm::ArrayRef<std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(data), size));
  default:
    llvm_unreachable("impossible blob type");
  }
}

memodb_value sqlite_db::get_obsolete(const memodb_ref &ref, bool binary_keys) {
  // Load an obsolete map value. Return a nested map where leaves are
  // memodb_ref.
  int value_type;
  {
    Stmt stmt(db, "SELECT type FROM value WHERE vid = ?1");
    stmt.bind_int(1, ref_to_id(ref));
    int rc = stmt.step();
    if (rc == SQLITE_DONE)
      return {}; // missing value
    if (rc != SQLITE_ROW)
      fatal_error();
    value_type = sqlite3_column_int64(stmt.stmt, 0);
  }

  if (value_type != OBSOLETE_MAP)
    return ref;

  // Load the map.
  auto result = memodb_value::map();
  {
    Stmt stmt(db, "SELECT key, value FROM map WHERE vid = ?1");
    stmt.bind_int(1, ref_to_id(ref));
    while (true) {
      int rc = stmt.step();
      if (rc == SQLITE_DONE)
        break;
      if (rc != SQLITE_ROW)
        fatal_error();
      memodb_ref value = id_to_ref(sqlite3_column_int64(stmt.stmt, 1));
      if (binary_keys) {
        const char *data =
            reinterpret_cast<const char *>(sqlite3_column_blob(stmt.stmt, 0));
        int size = sqlite3_column_bytes(stmt.stmt, 0);
        result[memodb_value::bytes(llvm::StringRef(data, size))] = value;
      } else {
        const char *key =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt.stmt, 0));
        result[key] = value;
      }
    }
  }

  // Load nested maps.
  for (auto &item : result.map_items()) {
    item.second = get_obsolete(item.second.as_ref(), /* binary_keys */ true);
  }

  return result;
}

std::vector<memodb_ref> sqlite_db::list_refs_using(const memodb_ref &ref) {
  std::vector<memodb_ref> result;
  Stmt stmt(db, "SELECT src FROM refs WHERE dest = ?1");
  stmt.bind_int(1, ref_to_id(ref));
  while (true) {
    auto rc = stmt.step();
    if (rc == SQLITE_DONE)
      break;
    if (rc != SQLITE_ROW)
      fatal_error();
    result.push_back(id_to_ref(sqlite3_column_int64(stmt.stmt, 0)));
  }
  return result;
}

std::vector<std::string> sqlite_db::list_heads() {
  std::vector<std::string> result;
  Stmt stmt(db, "SELECT name FROM head");
  while (true) {
    auto rc = stmt.step();
    if (rc == SQLITE_DONE)
      break;
    if (rc != SQLITE_ROW)
      fatal_error();
    result.emplace_back(
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.stmt, 0)));
  }
  return result;
}

std::vector<std::string> sqlite_db::list_heads_using(const memodb_ref &ref) {
  std::vector<std::string> result;
  Stmt stmt(db, "SELECT name FROM head WHERE vid = ?1");
  stmt.bind_int(1, ref_to_id(ref));
  while (true) {
    auto rc = stmt.step();
    if (rc == SQLITE_DONE)
      break;
    if (rc != SQLITE_ROW)
      fatal_error();
    result.emplace_back(
        reinterpret_cast<const char *>(sqlite3_column_text(stmt.stmt, 0)));
  }
  return result;
}

memodb_ref sqlite_db::head_get(llvm::StringRef name) {
  Stmt stmt(db, "SELECT vid FROM head WHERE name = ?1");
  stmt.bind_text(1, name);
  if (stmt.step() != SQLITE_ROW)
    return {};
  return id_to_ref(sqlite3_column_int64(stmt.stmt, 0));
}

void sqlite_db::head_set(llvm::StringRef name, const memodb_ref &value) {
  Stmt insert_stmt(db, "INSERT OR REPLACE INTO head(name, vid) VALUES(?1,?2)");
  insert_stmt.bind_text(1, name);
  insert_stmt.bind_int(2, ref_to_id(value));
  if (insert_stmt.step() != SQLITE_DONE)
    fatal_error();
}

std::unique_ptr<memodb_db> memodb_sqlite_open(llvm::StringRef path,
                                              bool create_if_missing) {
  auto db = std::make_unique<sqlite_db>();
  db->open(path.str().c_str(), create_if_missing);
  return db;
}

void sqlite_db::head_delete(llvm::StringRef name) {
  Stmt delete_stmt(db, "DELETE FROM head WHERE name = ?1");
  delete_stmt.bind_text(1, name);
  if (delete_stmt.step() != SQLITE_DONE)
    fatal_error();
}

sqlite3_int64 sqlite_db::get_fid(llvm::StringRef name, bool create_if_missing) {
  Stmt stmt(db, "SELECT fid FROM func WHERE name = ?1");
  stmt.bind_text(1, name);
  if (stmt.step() == SQLITE_ROW)
    return sqlite3_column_int64(stmt.stmt, 0);
  if (!create_if_missing)
    return -1;

  Stmt insert_stmt(db, "INSERT INTO func(name) VALUES (?1)");
  insert_stmt.bind_text(1, name);
  if (insert_stmt.step() != SQLITE_DONE)
    fatal_error();
  return sqlite3_last_insert_rowid(db);
}

memodb_ref sqlite_db::call_get(llvm::StringRef name,
                               llvm::ArrayRef<memodb_ref> args) {
  auto fid = get_fid(name);
  if (fid == -1)
    return {};

  Stmt stmt(db, "SELECT cid, result FROM call WHERE fid = ?1 AND parent IS ?2 "
                "AND arg = ?3");
  stmt.bind_int(1, fid);
  sqlite3_int64 parent = -1;
  for (const memodb_ref &arg : args) {
    stmt.reset();
    if (parent != -1)
      stmt.bind_int(2, parent);
    stmt.bind_int(3, ref_to_id(arg));
    if (stmt.step() != SQLITE_ROW)
      return {};
    parent = sqlite3_column_int64(stmt.stmt, 0);
  }

  return id_to_ref(sqlite3_column_int64(stmt.stmt, 1));
}

void sqlite_db::call_set(llvm::StringRef name, llvm::ArrayRef<memodb_ref> args,
                         const memodb_ref &result) {
  auto fid = get_fid(name, /* create_if_missing */ true);

  Stmt select_stmt(
      db, "SELECT cid FROM call WHERE fid = ?1 AND parent IS ?2 AND arg = ?3");
  Stmt insert_stmt(db, "INSERT INTO call(fid, parent, arg) VALUES(?1,?2,?3)");
  select_stmt.bind_int(1, fid);
  insert_stmt.bind_int(1, fid);
  sqlite3_int64 parent = -1;
  for (const memodb_ref &arg : args) {
    select_stmt.reset();
    insert_stmt.reset();
    if (parent != -1) {
      select_stmt.bind_int(2, parent);
      insert_stmt.bind_int(2, parent);
    }
    select_stmt.bind_int(3, ref_to_id(arg));
    insert_stmt.bind_int(3, ref_to_id(arg));
    if (select_stmt.step() == SQLITE_ROW) {
      parent = sqlite3_column_int64(select_stmt.stmt, 0);
    } else {
      if (insert_stmt.step() != SQLITE_DONE)
        fatal_error();
      parent = sqlite3_last_insert_rowid(db);
    }
  }

  Stmt update_stmt(db, "UPDATE call SET result = ?1 WHERE cid = ?2");
  update_stmt.bind_int(1, ref_to_id(result));
  update_stmt.bind_int(2, parent);
  if (update_stmt.step() != SQLITE_DONE)
    fatal_error();
}

void sqlite_db::call_invalidate(llvm::StringRef name) {
  auto fid = get_fid(name);
  if (fid == -1)
    return;

  Stmt delete_stmt(db, "DELETE FROM call WHERE fid = ?1");
  delete_stmt.bind_int(1, fid);
  if (delete_stmt.step() != SQLITE_DONE)
    fatal_error();
}
