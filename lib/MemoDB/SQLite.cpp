#include "memodb_internal.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/ScopedPrinter.h>

#include <cassert>
#include <cstring>
#include <mutex>
#include <sqlite3.h>
#include <vector>

using namespace memodb;

/* NOTE: we allow thread-safe access to sqlite_db by creating a separate
 * database connection for each thread. It may be worth experimenting with
 * shared caches <https://sqlite.org/sharedcache.html> as a trade-off between
 * RAM usage and lock contention. */

// Errors when running these statements are ignored.
static const std::vector<const char *> SQLITE_PRAGMAS = {
    // Don't enforce foreign key constraints.
    "PRAGMA foreign_keys = OFF;\n",

    // Use a WAL file instead of a journal, for efficiency.
    "PRAGMA journal_mode = WAL;\n",

    // Prevent corruption, but allow recent data to be lost if the computer
    // crashes.
    "PRAGMA synchronous = NORMAL;\n",

    // At checkpoints, truncate the WAL file if it's larger than 512 MiB.
    // The wal_hook() function will normally keep it smaller than that.
    "PRAGMA journal_size_limit = 536870912;\n",
};

static const unsigned int CURRENT_VERSION = 7;

static const int CODEC_RAW = 0;

static const char SQLITE_INIT_STMTS[] =
    "PRAGMA user_version = 7;\n"
    "PRAGMA application_id = 1111704642;\n"
    "CREATE TABLE blocks(\n"
    "  bid     INTEGER PRIMARY KEY,\n"
    "  cid     BLOB    NOT NULL UNIQUE,\n"
    "  codec   INTEGER NOT NULL,\n"
    "          -- compression type, etc.\n"
    "  content BLOB\n"
    ");\n"
    "CREATE TABLE heads(\n"
    "  name    TEXT    NOT NULL UNIQUE,\n"
    "  bid     INTEGER NOT NULL REFERENCES blocks(bid)\n"
    ");\n"
    "CREATE INDEX heads_by_bid ON heads(bid);\n"
    "CREATE TABLE funcs(\n"
    "  funcid  INTEGER PRIMARY KEY,\n"
    "  name    TEXT    NOT NULL UNIQUE\n"
    ");\n"
    "CREATE TABLE calls(\n"
    "  callid  INTEGER PRIMARY KEY,\n"
    "  funcid  INTEGER NOT NULL REFERENCES funcs(funcid),\n"
    "  args    BLOB    NOT NULL,\n"
    "          -- CBOR array with bids of arguments\n"
    "  result  INTEGER NOT NULL REFERENCES blocks(bid),\n"
    "  UNIQUE(funcid, args)\n"
    ");\n"
    "CREATE INDEX call_by_result ON calls(result, funcid);\n"
    "CREATE TABLE block_refs(\n"
    "  src     INTEGER NOT NULL REFERENCES blocks(bid),\n"
    "  dest    INTEGER NOT NULL REFERENCES blocks(bid),\n"
    "  UNIQUE(dest, src)\n"
    ");\n"
    "CREATE TABLE call_refs(\n"
    "  funcid  INTEGER NOT NULL REFERENCES funcs(funcid),\n"
    "  callid  INTEGER NOT NULL REFERENCES calls(callid),\n"
    "  dest    INTEGER NOT NULL REFERENCES blocks(bid),\n"
    "  UNIQUE(dest, funcid, callid)\n"
    ");\n"
    "CREATE INDEX call_ref_by_funcid ON call_refs(funcid);\n";

namespace {
class sqlite_db : public Store {
  // Used by each thread to look up its own connection to the database.
  // TODO: entries in this map are never removed, even when the sqlite_db is
  // destroyed, which could cause memory leaks.
  static thread_local llvm::DenseMap<sqlite_db *, sqlite3 *> thread_connections;

  // Used to make new connections to the database.
  std::string uri = {};

  // This field is used solely so that all threads' connections can be closed
  // in the single thread that calls the destructor.
  std::vector<sqlite3 *> open_connections = {};

  // Protects access to open_connections and uri.
  std::mutex mutex;

  // Get the current thread's database connection (creating a new connection if
  // necessary). The create_file_if_missing argument will cause a new database
  // file to be created if there isn't one.
  sqlite3 *get_db(bool create_file_if_missing = false);

  void fatal_error();
  void checkStatus(int rc);
  void checkDone(int rc);
  void requireRow(int rc);
  bool checkRow(int rc);

  CID bid_to_cid(sqlite3_int64 bid);
  sqlite3_int64 cid_to_bid(const CID &ref);
  sqlite3_int64 get_funcid(llvm::StringRef name,
                           bool create_if_missing = false);

  void add_refs_from(sqlite3_int64 id, const Node &value);

  void upgrade_schema();

  sqlite3_int64 putInternal(const CID &CID,
                            const llvm::ArrayRef<std::uint8_t> &Bytes,
                            const Node &Value);

  std::vector<std::uint8_t> encodeArgs(const Call &Call);
  Call identifyCall(sqlite3_int64 callid);

  friend class ExclusiveTransaction;

public:
  void open(const char *uri, bool create_if_missing);
  ~sqlite_db() override;

  llvm::Optional<Node> getOptional(const CID &CID) override;
  llvm::Optional<CID> resolveOptional(const Name &Name) override;
  CID put(const Node &value) override;
  void set(const Name &Name, const CID &ref) override;
  std::vector<Name> list_names_using(const CID &ref) override;
  std::vector<std::string> list_funcs() override;
  void eachHead(std::function<bool(const Head &)> F) override;
  void eachCall(llvm::StringRef Func,
                std::function<bool(const Call &)> F) override;
  void head_delete(const Head &Head) override;
  void call_invalidate(llvm::StringRef name) override;
};
} // end anonymous namespace

thread_local llvm::DenseMap<sqlite_db *, sqlite3 *>
    sqlite_db::thread_connections = llvm::DenseMap<sqlite_db *, sqlite3 *>();

namespace {
struct Stmt {
  sqlite3_stmt *stmt = nullptr;
  int rc;

  Stmt(sqlite3 *db, const char *sql) {
    rc = sqlite3_prepare_v2(db, sql, /*nByte*/ -1, &stmt, nullptr);
  }

  void bind_blob(int i, llvm::ArrayRef<std::uint8_t> Bytes) {
    if (rc != SQLITE_OK)
      return;
    rc =
        sqlite3_bind_blob64(stmt, i, Bytes.data(), Bytes.size(), SQLITE_STATIC);
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

  sqlite_int64 columnInt(int i) { return sqlite3_column_int64(stmt, i); }

  llvm::ArrayRef<std::uint8_t> columnBytes(int i) {
    const std::uint8_t *Data =
        reinterpret_cast<const std::uint8_t *>(sqlite3_column_blob(stmt, i));
    return llvm::ArrayRef(Data, sqlite3_column_bytes(stmt, i));
  }

  llvm::StringRef columnString(int i) {
    const char *Data =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
    return llvm::StringRef(Data, sqlite3_column_bytes(stmt, i));
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
class ExclusiveTransaction {
  sqlite_db &db;
  bool committed = false;

public:
  ExclusiveTransaction(sqlite_db &db) : db(db) {
    db.checkStatus(sqlite3_exec(db.get_db(), "BEGIN EXCLUSIVE", nullptr,
                                nullptr, nullptr));
  }
  void commit() {
    assert(!committed);
    committed = true;
    db.checkStatus(
        sqlite3_exec(db.get_db(), "COMMIT", nullptr, nullptr, nullptr));
  }
  ~ExclusiveTransaction() {
    if (!committed)
      sqlite3_exec(db.get_db(), "ROLLBACK", nullptr, nullptr, nullptr);
    // ignore return code
  }
};
} // end anonymous namespace

static int busy_callback(void *, int count) {
  int ms = 1;
  if (count >= 16) {
    unsigned total_seconds = (65535 + 65536 * (count - 16)) / 1000;
    ms = 65536;
    llvm::errs() << "database locked, still trying after " << total_seconds
                 << " seconds\n";
  } else {
    ms = 1 << count;
  }
  sqlite3_sleep(ms);
  return 1; // keep trying
}

static int wal_hook(void *, sqlite3 *db, const char *DatabaseName,
                    int NumPages) {
  // There are often so many concurrent readers that we get checkpoint
  // starvation, and the WAL file grows continuously:
  // https://sqlite.org/wal.html#avoiding_excessively_large_wal_files
  //
  // To prevent this, we use SQLITE_CHECKPOINT_RESTART, which causes readers to
  // block until the WAL file is completely flushed and we can restart from the
  // beginning.

  if (NumPages < 16384) // 64 MiB with default page size
    return SQLITE_OK;
  int rc = sqlite3_wal_checkpoint_v2(
      db, DatabaseName, SQLITE_CHECKPOINT_RESTART, nullptr, nullptr);
  if (rc == SQLITE_BUSY)
    return SQLITE_OK; // Another thread is already running a checkpoint.
  return rc;
}

sqlite3 *sqlite_db::get_db(bool create_file_if_missing) {
  sqlite3 *&result = thread_connections[this];

  if (!result) {
    const std::lock_guard<std::mutex> lock(mutex);

    int flags = SQLITE_OPEN_URI | SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX |
                (create_file_if_missing ? SQLITE_OPEN_CREATE : 0);
    checkStatus(sqlite3_open_v2(uri.c_str(), &result, flags, /*zVfs*/ nullptr));

    checkStatus(sqlite3_busy_handler(result, busy_callback, nullptr));
    sqlite3_wal_hook(result, wal_hook, nullptr);

    for (const char *stmt : SQLITE_PRAGMAS) {
      sqlite3_exec(result, stmt, nullptr, nullptr, nullptr);
      // ignore return code
    }
    upgrade_schema();

    open_connections.push_back(result);
  }

  return result;
}

void sqlite_db::fatal_error() {
  llvm::report_fatal_error(sqlite3_errmsg(get_db()));
}

void sqlite_db::checkStatus(int rc) {
  if (rc != SQLITE_OK)
    fatal_error();
}

void sqlite_db::checkDone(int rc) {
  if (rc != SQLITE_DONE)
    fatal_error();
}

void sqlite_db::requireRow(int rc) {
  if (rc != SQLITE_ROW)
    fatal_error();
}

bool sqlite_db::checkRow(int rc) {
  if (rc == SQLITE_ROW)
    return true;
  else if (rc == SQLITE_DONE)
    return false;
  fatal_error();
  return false;
}

void sqlite_db::open(const char *uri, bool create_if_missing) {
  // We don't need to lock the mutex, since only the thread calling
  // memodb_sqlite_open knows about the sqlite_db at this point.
  assert(open_connections.empty());
  this->uri = uri;
  get_db(create_if_missing);
}

sqlite_db::~sqlite_db() {
  sqlite3_exec(get_db(), "PRAGMA optimize;", nullptr, nullptr, nullptr);
  // ignore return code

  const std::lock_guard<std::mutex> lock(mutex);
  for (sqlite3 *db : open_connections)
    sqlite3_close(db);
}

void sqlite_db::upgrade_schema() {
  sqlite3 *db = get_db();

  // Exit early if the schema is already current.
  sqlite3_int64 user_version;
  {
    Stmt stmt(db, "PRAGMA user_version");
    requireRow(stmt.step());
    user_version = stmt.columnInt(0);
  }
  if (user_version == CURRENT_VERSION)
    return;

  // Start an exclusive transaction so the upgrade process doesn't conflict
  // with other processes.
  ExclusiveTransaction transaction(*this);

  // If the database is empty, initialize it.
  {
    Stmt exists_stmt(
        db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='value'");
    if (!checkRow(exists_stmt.step()))
      checkStatus(
          sqlite3_exec(db, SQLITE_INIT_STMTS, nullptr, nullptr, nullptr));
  }

  {
    Stmt stmt(db, "PRAGMA user_version");
    requireRow(stmt.step());
    user_version = stmt.columnInt(0);
  }

  if (user_version > CURRENT_VERSION) {
    llvm::errs() << "The BCDB format is too new (this BCDB file uses format "
                 << user_version << ", but we only support format "
                 << CURRENT_VERSION << ")\n";
    llvm::errs() << "Please upgrade your BCDB software!\n";
    fatal_error();
  }

  if (user_version < CURRENT_VERSION) {
    llvm::errs() << "This BCDB database is too old to read. BCDB's "
                    "legacy-sqlite tag from Git should be able to read it and "
                    "convert it to CAR or RocksDB.\n";
    fatal_error();
  }

  // NOTE: it might be nice to run VACUUM here after converting. However, it
  // can be extremely slow and it requires either gigabytes of RAM or gigabytes
  // of /tmp space (depending on the value of PRAGMA temp_store).

  transaction.commit();

  // Ensure the new user_version/application_id are written to the actual
  // database file.
  sqlite3_exec(db, "PRAGMA wal_checkpoint(FULL);", nullptr, nullptr, nullptr);
  // ignore return code
}

CID sqlite_db::bid_to_cid(sqlite3_int64 bid) {
  sqlite3 *db = get_db();
  Stmt stmt(db, "SELECT cid FROM blocks WHERE bid = ?1");
  stmt.bind_int(1, bid);
  requireRow(stmt.step());
  return *CID::fromBytes(stmt.columnBytes(0));
}

sqlite3_int64 sqlite_db::cid_to_bid(const CID &ref) {
  sqlite3 *db = get_db();
  Stmt stmt(db, "SELECT bid FROM blocks WHERE cid = ?1");
  stmt.bind_blob(1, ref.asBytes());
  if (checkRow(stmt.step()))
    return stmt.columnInt(0);

  if (!ref.isIdentity())
    fatal_error();
  std::vector<std::uint8_t> Content;
  Node Value = Node::loadFromIPLD(ref, {});
  Value.save_cbor(Content);
  return putInternal(ref, Content, Value);
}

sqlite3_int64 sqlite_db::putInternal(const CID &CID,
                                     const llvm::ArrayRef<std::uint8_t> &Bytes,
                                     const Node &Value) {
  sqlite3 *db = get_db();

  // Optimistically check for an existing entry (without a transaction).
  {
    Stmt stmt(db, "SELECT bid FROM blocks WHERE cid = ?1");
    stmt.bind_blob(1, CID.asBytes());
    if (checkRow(stmt.step()))
      return stmt.columnInt(0);
  }

  // We may need to add a new entry. Start an exclusive transaction (if we
  // aren't already in one) and check again (an entry may have been added since
  // the previous check).
  std::optional<ExclusiveTransaction> transaction;
  if (sqlite3_txn_state(db, nullptr) == SQLITE_TXN_NONE)
    transaction.emplace(*this);

  {
    Stmt stmt(db, "SELECT bid FROM blocks WHERE cid = ?1");
    stmt.bind_blob(1, CID.asBytes());
    if (checkRow(stmt.step()))
      return stmt.columnInt(0);
  }

  // Add the new entry to the blocks table.
  sqlite3_int64 new_id;
  {
    Stmt stmt(db, "INSERT INTO blocks(cid,codec,content) VALUES (?1,?2,?3)");
    stmt.bind_blob(1, CID.asBytes());
    stmt.bind_int(2, CODEC_RAW);
    stmt.bind_blob(3, Bytes);
    checkDone(stmt.step());
    new_id = sqlite3_last_insert_rowid(db);
    assert(new_id);
  }

  // Update the refs table.
  add_refs_from(new_id, Value);

  if (transaction)
    transaction->commit();
  return new_id;
}

std::vector<std::uint8_t> sqlite_db::encodeArgs(const Call &Call) {
  Node ArgsValue = Node(node_list_arg);
  for (const CID &Arg : Call.Args)
    ArgsValue.emplace_back(cid_to_bid(Arg));
  std::vector<std::uint8_t> Result;
  ArgsValue.save_cbor(Result);
  return Result;
}

Call sqlite_db::identifyCall(sqlite3_int64 callid) {
  sqlite3 *db = get_db();
  Stmt stmt(
      db, "SELECT name, args FROM calls NATURAL JOIN funcs WHERE callid = ?1");
  stmt.bind_int(1, callid);
  requireRow(stmt.step());

  std::vector<CID> Args;
  Node ArgsValue = Node::load_cbor(stmt.columnBytes(1));
  for (const Node &ArgValue : ArgsValue.list_range())
    Args.emplace_back(bid_to_cid(ArgValue.as<sqlite3_int64>()));

  return Call(stmt.columnString(0), Args);
}

CID sqlite_db::put(const Node &value) {
  auto IPLD = value.saveAsIPLD();
  putInternal(IPLD.first, IPLD.second, value);
  return IPLD.first;
}

void sqlite_db::set(const Name &Name, const CID &ref) {
  sqlite3 *db = get_db();
  if (const Head *head = std::get_if<Head>(&Name)) {
    Stmt stmt(db, "INSERT OR REPLACE INTO heads(name, bid) VALUES(?1,?2)");
    stmt.bind_text(1, head->Name);
    stmt.bind_int(2, cid_to_bid(ref));
    checkDone(stmt.step());
  } else if (const Call *call = std::get_if<Call>(&Name)) {
    auto funcid = get_funcid(call->Name, /* create_if_missing */ true);

    ExclusiveTransaction transaction(*this);

    auto Args = encodeArgs(*call);

    bool existing;
    sqlite3_int64 CallID;
    {
      Stmt stmt(db, "SELECT callid FROM calls WHERE funcid = ?1 AND args = ?2");
      stmt.bind_int(1, funcid);
      stmt.bind_blob(2, Args);
      existing = checkRow(stmt.step());
      if (existing)
        CallID = stmt.columnInt(0);
    }

    if (existing) {
      // The existing call_refs rows don't need to change.
      Stmt stmt(db, "UPDATE calls SET result = ?1 WHERE callid = ?2");
      stmt.bind_int(1, cid_to_bid(ref));
      stmt.bind_int(2, CallID);
      checkDone(stmt.step());
    } else {
      {
        Stmt stmt(db,
                  "INSERT INTO calls(funcid, args, result) VALUES(?1,?2,?3)");
        stmt.bind_int(1, funcid);
        stmt.bind_blob(2, Args);
        stmt.bind_int(3, cid_to_bid(ref));
        checkDone(stmt.step());
        CallID = sqlite3_last_insert_rowid(db);
      }
      {
        Stmt stmt(db, "INSERT OR IGNORE INTO call_refs(funcid, callid, dest) "
                      "VALUES(?1,?2,?3)");
        for (const CID &Arg : call->Args) {
          stmt.bind_int(1, funcid);
          stmt.bind_int(2, CallID);
          stmt.bind_int(3, cid_to_bid(Arg));
          checkDone(stmt.step());
          stmt.reset();
        }
      }
    }
    transaction.commit();
  } else {
    llvm::report_fatal_error("can't set a CID");
  }
}

void sqlite_db::add_refs_from(sqlite3_int64 id, const Node &value) {
  sqlite3 *db = get_db();
  value.eachLink([&](const CID &Link) {
    auto dest = cid_to_bid(Link);
    Stmt stmt(db, "INSERT OR IGNORE INTO block_refs(src, dest) VALUES (?1,?2)");
    stmt.bind_int(1, id);
    stmt.bind_int(2, dest);
    checkDone(stmt.step());
  });
}

llvm::Optional<Node> sqlite_db::getOptional(const CID &CID) {
  if (CID.isIdentity())
    return Node::loadFromIPLD(CID, {});
  sqlite3 *db = get_db();
  Stmt stmt(db, "SELECT codec, content FROM blocks WHERE cid = ?1");
  stmt.bind_blob(1, CID.asBytes());
  if (!checkRow(stmt.step()))
    return llvm::None;
  if (stmt.columnInt(0) != CODEC_RAW)
    llvm::report_fatal_error("unsupported compression codec");
  return Node::loadFromIPLD(CID, stmt.columnBytes(1));
}

llvm::Optional<CID> sqlite_db::resolveOptional(const Name &Name) {
  if (const CID *Ref = std::get_if<CID>(&Name)) {
    return *Ref;
  } else if (const Head *head = std::get_if<Head>(&Name)) {
    sqlite3 *db = get_db();
    Stmt stmt(db, "SELECT bid FROM heads WHERE name = ?1");
    stmt.bind_text(1, head->Name);
    if (!checkRow(stmt.step()))
      return llvm::None;
    return bid_to_cid(stmt.columnInt(0));
  } else if (const Call *call = std::get_if<Call>(&Name)) {
    sqlite3 *db = get_db();
    auto funcid = get_funcid(call->Name);
    auto Args = encodeArgs(*call);
    Stmt stmt(db, "SELECT result FROM calls WHERE funcid = ?1 AND args = ?2");
    stmt.bind_int(1, funcid);
    stmt.bind_blob(2, Args);
    if (!checkRow(stmt.step()))
      return llvm::None;
    return bid_to_cid(stmt.columnInt(0));
  } else {
    llvm_unreachable("impossible Name type");
  }
}

std::vector<Name> sqlite_db::list_names_using(const CID &ref) {
  sqlite3 *db = get_db();
  std::vector<Name> Result;

  auto BID = cid_to_bid(ref);

  {
    Stmt stmt(db, "SELECT src FROM block_refs WHERE dest = ?1");
    stmt.bind_int(1, BID);
    while (checkRow(stmt.step()))
      Result.emplace_back(bid_to_cid(stmt.columnInt(0)));
  }

  {
    Stmt stmt(db, "SELECT name FROM heads WHERE bid = ?1");
    stmt.bind_int(1, BID);
    while (checkRow(stmt.step()))
      Result.emplace_back(Head(stmt.columnString(0)));
  }

  {
    Stmt stmt(db, "SELECT callid FROM calls WHERE result = ?1");
    stmt.bind_int(1, BID);
    while (checkRow(stmt.step()))
      Result.emplace_back(identifyCall(stmt.columnInt(0)));
  }

  {
    Stmt stmt(db, "SELECT callid FROM call_refs WHERE dest = ?1");
    stmt.bind_int(1, BID);
    while (checkRow(stmt.step()))
      Result.emplace_back(identifyCall(stmt.columnInt(0)));
  }

  return Result;
}

void sqlite_db::eachCall(llvm::StringRef Func,
                         std::function<bool(const Call &)> F) {
  sqlite3 *db = get_db();
  sqlite3_int64 FuncID = get_funcid(Func);
  Stmt stmt(db, "SELECT callid FROM calls WHERE funcid = ?");
  stmt.bind_int(1, FuncID);
  while (checkRow(stmt.step()))
    if (F(identifyCall(stmt.columnInt(0))))
      break;
}

std::vector<std::string> sqlite_db::list_funcs() {
  sqlite3 *db = get_db();
  std::vector<std::string> result;
  Stmt stmt(db, "SELECT name FROM funcs");
  while (checkRow(stmt.step()))
    result.emplace_back(stmt.columnString(0));
  return result;
}

void sqlite_db::eachHead(std::function<bool(const Head &)> F) {
  sqlite3 *db = get_db();
  Stmt stmt(db, "SELECT name FROM heads");
  while (checkRow(stmt.step()))
    if (F(Head(stmt.columnString(0))))
      break;
}

void sqlite_db::head_delete(const Head &Head) {
  sqlite3 *db = get_db();
  Stmt delete_stmt(db, "DELETE FROM heads WHERE name = ?1");
  delete_stmt.bind_text(1, Head.Name);
  checkDone(delete_stmt.step());
}

sqlite3_int64 sqlite_db::get_funcid(llvm::StringRef name,
                                    bool create_if_missing) {
  sqlite3 *db = get_db();
  Stmt stmt(db, "SELECT funcid FROM funcs WHERE name = ?1");
  stmt.bind_text(1, name);
  if (checkRow(stmt.step()))
    return stmt.columnInt(0);
  if (!create_if_missing)
    return -1;

  ExclusiveTransaction transaction(*this);
  stmt.reset();
  if (checkRow(stmt.step()))
    return stmt.columnInt(0);

  Stmt insert_stmt(db, "INSERT INTO funcs(name) VALUES (?1)");
  insert_stmt.bind_text(1, name);
  checkDone(insert_stmt.step());
  sqlite3_int64 newid = sqlite3_last_insert_rowid(db);
  assert(newid > 0);
  transaction.commit();
  return newid;
}

void sqlite_db::call_invalidate(llvm::StringRef name) {
  sqlite3 *db = get_db();
  auto funcid = get_funcid(name);

  ExclusiveTransaction transaction(*this);
  {
    Stmt stmt(db, "DELETE FROM calls WHERE funcid = ?1");
    stmt.bind_int(1, funcid);
    checkDone(stmt.step());
  }
  {
    Stmt stmt(db, "DELETE FROM call_refs WHERE funcid = ?1");
    stmt.bind_int(1, funcid);
    checkDone(stmt.step());
  }
  transaction.commit();
}

std::unique_ptr<Store> memodb_sqlite_open(llvm::StringRef path,
                                          bool create_if_missing) {
  auto uri = "file:" + path;
  auto db = std::make_unique<sqlite_db>();
  db->open(uri.str().c_str(), create_if_missing);
  return db;
}
