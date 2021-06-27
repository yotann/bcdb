#include "memodb_internal.h"

#if BCDB_WITH_ROCKSDB

#include <functional>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <map>
#include <memory>
#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>
#include <string>
#include <vector>

using namespace memodb;

/*
 * Column families:
 *
 * "default"
 * - Contains ("format", "MemoDB").
 * - Contains ("version", version number as string).
 *
 * "blocks"
 * - Contains (CID, data) for every IPLD block stored.
 *
 * "heads"
 * - Contains (name, CID) for every head.
 *
 * "calls"
 * - Contains (CBOR(function_name) + CID(arg0) + CID(arg1) + ..., CID(result))
 *   for every call.
 *
 * "refs"
 * - Contains (UsedCID + TYPE_BLOCK + UserCID, "") for every block UserCID that
 *   contains a reference to a block UsedCID.
 * - Contains (UsedCID + TYPE_HEAD + name, "") for every head.
 * - Contains (UsedCID + TYPE_CALL + CBOR(function_name) + CID(arg0) +
 *   CID(arg1) + ..., "") for every call's result and arguments.
 *
 * NOTE: as an alternative, it would be possible to store hashes of call
 * arguments instead of putting the arguments directly in the key. This would
 * save space in the "refs" column family, but the "calls" column family would
 * need an extra row to contain the actual arguments. I tested this change on a
 * couple databases with 1-3 argument functions and it actually made them
 * slightly *larger*, so it doesn't seem promising.
 *
 * NOTE: as another alternative, we could store each call in its own family.
 * This would permit invalidating a call by dropping the family, but we don't
 * want to do that anyway because it would leave dangling refs.
 */

static const char TYPE_BLOCK = 'b';
static const char TYPE_CALL = 'c';
static const char TYPE_HEAD = 'h';

static llvm::ArrayRef<uint8_t> makeBytes(const rocksdb::Slice &Slice) {
  return llvm::ArrayRef(reinterpret_cast<const uint8_t *>(Slice.data()),
                        Slice.size());
}

static rocksdb::Slice makeSlice(const llvm::ArrayRef<uint8_t> &Bytes) {
  return rocksdb::Slice(reinterpret_cast<const char *>(Bytes.data()),
                        Bytes.size());
}

namespace {
class RocksDBStore : public Store {
private:
  std::unique_ptr<rocksdb::OptimisticTransactionDB> DB;
  rocksdb::ColumnFamilyHandle *DefaultFamily;
  rocksdb::ColumnFamilyHandle *BlocksFamily;
  rocksdb::ColumnFamilyHandle *CallsFamily;
  rocksdb::ColumnFamilyHandle *HeadsFamily;
  rocksdb::ColumnFamilyHandle *RefsFamily;

  void checkStatus(const rocksdb::Status &Status);
  bool checkFound(const rocksdb::Status &Status);

  template <typename BatchT>
  void addRef(BatchT &Batch, char Type, const rocksdb::Slice &From,
              const CID &To);
  template <typename BatchT>
  void deleteRef(BatchT &Batch, char Type, const rocksdb::Slice &From,
                 const rocksdb::Slice &To);
  void addRefs(rocksdb::WriteBatch &Batch, char Type,
               const llvm::ArrayRef<uint8_t> &Key, const Node &Value);

  std::string makeKeyForCall(const Call &Call);

public:
  void open(llvm::StringRef uri, bool create_if_missing);
  ~RocksDBStore() override;

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

void RocksDBStore::checkStatus(const rocksdb::Status &Status) {
  if (!Status.ok())
    llvm::report_fatal_error("RocksDB error: " + Status.ToString());
}

bool RocksDBStore::checkFound(const rocksdb::Status &Status) {
  if (Status.IsNotFound())
    return false;
  checkStatus(Status);
  return true;
}

template <typename BatchT>
void RocksDBStore::addRef(BatchT &Batch, char Type, const rocksdb::Slice &From,
                          const CID &To) {
  if (To.isIdentity())
    return;
  rocksdb::Slice Slices[3] = {
      makeSlice(To.asBytes()),
      rocksdb::Slice(&Type, 1),
      From,
  };
  rocksdb::SliceParts Key(Slices, 3);
  Batch.Put(RefsFamily, Key, {});
}

template <typename BatchT>
void RocksDBStore::deleteRef(BatchT &Batch, char Type,
                             const rocksdb::Slice &From,
                             const rocksdb::Slice &To) {
  rocksdb::Slice Slices[3] = {
      To,
      rocksdb::Slice(&Type, 1),
      From,
  };
  rocksdb::SliceParts Key(Slices, 3);
  Batch.Delete(RefsFamily, Key, {});
}

void RocksDBStore::addRefs(rocksdb::WriteBatch &Batch, char Type,
                           const llvm::ArrayRef<uint8_t> &Key,
                           const Node &Value) {
  Value.eachLink([&](const auto &Link) {
    if (!Link.isIdentity())
      addRef(Batch, Type, makeSlice(Key), Link);
  });
}

std::string RocksDBStore::makeKeyForCall(const Call &Call) {
  std::vector<std::uint8_t> Buffer;
  Node(utf8_string_arg, Call.Name).save_cbor(Buffer);
  std::string Key = makeSlice(Buffer).ToString();
  for (const CID &Arg : Call.Args) {
    auto CID = Arg.asBytes();
    Key.insert(Key.end(), CID.begin(), CID.end());
  }
  return Key;
}

void RocksDBStore::open(llvm::StringRef uri, bool create_if_missing) {
  ParsedURI Parsed(uri);
  if (Parsed.Scheme != "rocksdb" || !Parsed.Authority.empty() ||
      !Parsed.Query.empty() || !Parsed.Fragment.empty())
    llvm::report_fatal_error("Unsupported RocksDB URI");

  rocksdb::ColumnFamilyOptions BaseCFOptions;
  rocksdb::DBOptions DBOptions;
  rocksdb::BlockBasedTableOptions TableOptions;

  DBOptions.create_if_missing = create_if_missing;
  DBOptions.create_missing_column_families = create_if_missing;

  // https://github.com/facebook/rocksdb/wiki/Setup-Options-and-Basic-Tuning
  // Some obsolete options from that page have been replaced.
  TableOptions.block_cache = rocksdb::NewLRUCache(256 << 20);
  BaseCFOptions.compression = rocksdb::kLZ4Compression;
  BaseCFOptions.bottommost_compression = rocksdb::kZSTD;
  TableOptions.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
  TableOptions.optimize_filters_for_memory = true;
  BaseCFOptions.level_compaction_dynamic_level_bytes = true;
  DBOptions.IncreaseParallelism(16);
  DBOptions.bytes_per_sync = 1 << 20;
  TableOptions.block_size = 16 << 10;
  TableOptions.cache_index_and_filter_blocks = true;
  TableOptions.metadata_cache_options.partition_pinning =
      rocksdb::PinningTier::kFlushedAndSimilar;
  TableOptions.metadata_cache_options.unpartitioned_pinning =
      rocksdb::PinningTier::kFlushedAndSimilar;
  TableOptions.format_version = 5; // Requires RocksDB 6.6.0

  // Options set by ColumnFamilyOptions::OptimizeForPointLookup
  TableOptions.data_block_index_type =
      rocksdb::BlockBasedTableOptions::kDataBlockBinaryAndHash;
  BaseCFOptions.memtable_prefix_bloom_size_ratio = 0.02;
  BaseCFOptions.memtable_whole_key_filtering = true;

  // Well-trained dictionaries can reduce database size by >50%.
  BaseCFOptions.compression_opts.enabled = true;
  BaseCFOptions.compression_opts.max_dict_bytes = 64 << 10;
  BaseCFOptions.compression_opts.zstd_max_train_bytes = 16 << 20;
  BaseCFOptions.bottommost_compression_opts.enabled = true;
  BaseCFOptions.bottommost_compression_opts.max_dict_bytes = 64 << 10;
  BaseCFOptions.bottommost_compression_opts.zstd_max_train_bytes = 16 << 20;

  // We want fewer, larger blob files and table files, to make sure huge
  // databases have a reasonable number of files.
  BaseCFOptions.write_buffer_size = 256 << 20;
  BaseCFOptions.target_file_size_base =
      BaseCFOptions.write_buffer_size *
      BaseCFOptions.min_write_buffer_number_to_merge;
  BaseCFOptions.max_bytes_for_level_base =
      BaseCFOptions.target_file_size_base * 4;

  // These options are important because we write to column families unevenly
  // (e.g., writing GBs of data to BlocksFamily but only KBs to other
  // families). The db_write_buffer_size option limits total write buffer size
  // across all CFs, so we don't end up with a 100MB write buffer for a CF
  // that's barely written. The max_total_wal_size option flushes
  // rarely-written CFs occasionally to ensure old log files can be deleted.
  DBOptions.db_write_buffer_size = BaseCFOptions.write_buffer_size * 3;
  DBOptions.max_total_wal_size = DBOptions.db_write_buffer_size * 4;

  // With our 256MB block cache and 768MB db_write_buffer_size, total memory
  // usage by RocksDB should be ~1GB.

  // Prevent EMFILE error when opening too many files.
  DBOptions.max_open_files = 1024;

  BaseCFOptions.table_factory.reset(
      rocksdb::NewBlockBasedTableFactory(TableOptions));

  // The blocks column family may hold arbitrarily large values. Enable BlobDB
  // to put large values in separate files, so they aren't repeatedly rewritten
  // by the compaction process. Note that blobs do not currently support
  // compression dictionaries, so blobs smaller than ~64KB waste space.
  auto BlocksCFOptions = BaseCFOptions;
  BlocksCFOptions.enable_blob_files = true;
  BlocksCFOptions.blob_compression_type = rocksdb::kZSTD;
  BlocksCFOptions.min_blob_size = 64 << 10;
  // Combine blob files into larger ones.
  // BlocksCFOptions.enable_blob_garbage_collection = true;

  std::vector<rocksdb::ColumnFamilyDescriptor> FamilyDescs;
  FamilyDescs.emplace_back(rocksdb::kDefaultColumnFamilyName, BaseCFOptions);
  FamilyDescs.emplace_back("blocks", BlocksCFOptions);
  FamilyDescs.emplace_back("calls", BaseCFOptions);
  FamilyDescs.emplace_back("heads", BaseCFOptions);
  FamilyDescs.emplace_back("refs", BaseCFOptions);

  std::vector<rocksdb::ColumnFamilyHandle *> FamilyHandles;
  rocksdb::OptimisticTransactionDB *TmpDB;
  checkStatus(rocksdb::OptimisticTransactionDB::Open(
      DBOptions, {}, Parsed.Path, FamilyDescs, &FamilyHandles, &TmpDB));
  DB.reset(TmpDB);

  assert(FamilyHandles.size() == 5);
  DefaultFamily = FamilyHandles[0];
  BlocksFamily = FamilyHandles[1];
  CallsFamily = FamilyHandles[2];
  HeadsFamily = FamilyHandles[3];
  RefsFamily = FamilyHandles[4];

  std::unique_ptr<rocksdb::Iterator> Iterator(
      DB->NewIterator({}, DefaultFamily));
  Iterator->SeekToFirst();
  if (Iterator->Valid()) {
    // existing database, verify magic values
    rocksdb::PinnableSlice Fetched;
    if (!checkFound(DB->Get({}, DefaultFamily, "format", &Fetched)) ||
        Fetched != "MemoDB")
      llvm::report_fatal_error("this is not a MemoDB database");
    if (!checkFound(DB->Get({}, DefaultFamily, "version", &Fetched)) ||
        Fetched != "0")
      llvm::report_fatal_error("unsupported database version");
  } else {
    checkStatus(Iterator->status());
    // empty database, insert magic values
    checkStatus(DB->Put({}, DefaultFamily, "format", "MemoDB"));
    checkStatus(DB->Put({}, DefaultFamily, "version", "0"));
  }
}

RocksDBStore::~RocksDBStore() {
  checkStatus(DB->FlushWAL(true));
  checkStatus(DB->DestroyColumnFamilyHandle(DefaultFamily));
  checkStatus(DB->DestroyColumnFamilyHandle(BlocksFamily));
  checkStatus(DB->DestroyColumnFamilyHandle(CallsFamily));
  checkStatus(DB->DestroyColumnFamilyHandle(HeadsFamily));
  checkStatus(DB->DestroyColumnFamilyHandle(RefsFamily));
  checkStatus(DB->Close());
}

llvm::Optional<Node> RocksDBStore::getOptional(const CID &CID) {
  if (CID.isIdentity())
    return Node::loadFromIPLD(CID, {});
  rocksdb::PinnableSlice Fetched;
  if (!checkFound(
          DB->Get({}, BlocksFamily, makeSlice(CID.asBytes()), &Fetched)))
    return {};
  return Node::loadFromIPLD(CID, makeBytes(Fetched));
}

llvm::Optional<CID> RocksDBStore::resolveOptional(const Name &Name) {
  if (const CID *Ref = std::get_if<CID>(&Name)) {
    return *Ref;
  } else if (const Head *head = std::get_if<Head>(&Name)) {
    rocksdb::PinnableSlice Fetched;
    if (!checkFound(DB->Get({}, HeadsFamily, head->Name, &Fetched)))
      return {};
    return *CID::fromBytes(makeBytes(Fetched));
  } else if (const Call *call = std::get_if<Call>(&Name)) {
    auto Key = makeKeyForCall(*call);
    rocksdb::PinnableSlice Fetched;
    if (!checkFound(DB->Get({}, CallsFamily, Key, &Fetched)))
      return {};
    return *CID::fromBytes(makeBytes(Fetched));
  } else {
    llvm_unreachable("impossible Name type");
  }
}

CID RocksDBStore::put(const Node &value) {
  auto IPLD = value.saveAsIPLD();
  if (IPLD.second.empty())
    return IPLD.first;
  auto Key = IPLD.first.asBytes();

  rocksdb::PinnableSlice Fetched;
  if (checkFound(DB->Get({}, BlocksFamily, makeSlice(Key), &Fetched))) {
    assert(makeSlice(IPLD.second) == Fetched);
    return IPLD.first;
  }

  rocksdb::WriteBatch Batch;
  Batch.Put(BlocksFamily, makeSlice(Key), makeSlice(IPLD.second));
  addRefs(Batch, TYPE_BLOCK, Key, value);
  checkStatus(DB->Write({}, &Batch));
  return IPLD.first;
}

void RocksDBStore::set(const Name &Name, const CID &ref) {
  rocksdb::Status TxnStatus;
  auto refKey = ref.asBytes();
  do {
    std::unique_ptr<rocksdb::Transaction> Txn(DB->BeginTransaction({}, {}));
    if (const Head *head = std::get_if<Head>(&Name)) {
      rocksdb::PinnableSlice Fetched;
      if (checkFound(Txn->GetForUpdate({}, HeadsFamily, head->Name, &Fetched)))
        deleteRef(*Txn, TYPE_HEAD, head->Name, Fetched);
      Txn->Put(HeadsFamily, head->Name, makeSlice(refKey));
      addRef(*Txn, TYPE_HEAD, head->Name, ref);
    } else if (const Call *call = std::get_if<Call>(&Name)) {
      auto Key = makeKeyForCall(*call);
      rocksdb::PinnableSlice Fetched;
      if (checkFound(Txn->GetForUpdate({}, CallsFamily, Key, &Fetched)))
        deleteRef(*Txn, TYPE_CALL, Key, Fetched);
      Txn->Put(CallsFamily, Key, makeSlice(refKey));
      addRef(*Txn, TYPE_CALL, Key, ref);
      for (const CID &Arg : call->Args)
        addRef(*Txn, TYPE_CALL, Key, Arg);
    } else {
      llvm::report_fatal_error("can't set a CID");
    }
    TxnStatus = Txn->Commit();
  } while (TxnStatus.IsBusy() || TxnStatus.IsTryAgain());
  checkStatus(TxnStatus);
}

std::vector<Name> RocksDBStore::list_names_using(const CID &ref) {
  std::vector<Name> Result;
  auto Key = ref.asBytes();
  std::unique_ptr<rocksdb::Iterator> Iterator(DB->NewIterator({}, RefsFamily));
  for (Iterator->Seek(makeSlice(Key)); Iterator->Valid(); Iterator->Next()) {
    auto Ref = Iterator->key();
    if (!Ref.starts_with(makeSlice(Key)))
      break;
    Ref.remove_prefix(Key.size());
    if (Ref.empty())
      llvm::report_fatal_error("missing type in refs family");
    char Type = Ref[0];
    Ref.remove_prefix(1);
    if (Type == TYPE_BLOCK) {
      Result.emplace_back(*CID::fromBytes(makeBytes(Ref)));
    } else if (Type == TYPE_HEAD) {
      Result.emplace_back(Head(Ref.ToString()));
    } else if (Type == TYPE_CALL) {
      auto Bytes = makeBytes(Ref);
      Call Call("", {});
      Call.Name = Node::load_cbor_from_sequence(Bytes).as<std::string>();
      while (!Bytes.empty())
        Call.Args.emplace_back(*CID::loadFromSequence(Bytes));
      Result.emplace_back(std::move(Call));
    } else {
      llvm::report_fatal_error("invalid type in refs family");
    }
  }
  checkStatus(Iterator->status());
  return Result;
}

std::vector<std::string> RocksDBStore::list_funcs() {
  std::vector<std::string> Result;
  std::unique_ptr<rocksdb::Iterator> Iterator(DB->NewIterator({}, CallsFamily));
  for (Iterator->SeekToFirst(); Iterator->Valid();) {
    auto Bytes = makeBytes(Iterator->key());
    Result.emplace_back(
        Node::load_cbor_from_sequence(Bytes).as<llvm::StringRef>());

    std::vector<std::uint8_t> NextKey =
        makeBytes(Iterator->key()).drop_back(Bytes.size());
    for (auto I = NextKey.rbegin(), IE = NextKey.rend(); I != IE; I++)
      if (++(*I))
        break;
    Iterator->Seek(makeSlice(NextKey));
  }
  checkStatus(Iterator->status());
  return Result;
}

void RocksDBStore::eachHead(std::function<bool(const Head &)> F) {
  std::unique_ptr<rocksdb::Iterator> Iterator(DB->NewIterator({}, HeadsFamily));
  for (Iterator->SeekToFirst(); Iterator->Valid(); Iterator->Next())
    if (F(Head(Iterator->key().ToString())))
      break;
  checkStatus(Iterator->status());
}

void RocksDBStore::eachCall(llvm::StringRef Func,
                            std::function<bool(const Call &)> F) {
  std::vector<std::uint8_t> Prefix;
  Node(utf8_string_arg, Func).save_cbor(Prefix);
  std::unique_ptr<rocksdb::Iterator> Iterator(DB->NewIterator({}, CallsFamily));
  for (Iterator->Seek(makeSlice(Prefix)); Iterator->Valid(); Iterator->Next()) {
    if (!makeBytes(Iterator->key()).take_front(Prefix.size()).equals(Prefix))
      break;
    auto Bytes = makeBytes(Iterator->key()).drop_front(Prefix.size());
    Call Call(Func, {});
    while (!Bytes.empty())
      Call.Args.emplace_back(*CID::loadFromSequence(Bytes));
    if (F(Call))
      return;
  }
  checkStatus(Iterator->status());
}

void RocksDBStore::head_delete(const Head &Head) {
  rocksdb::Status TxnStatus;
  do {
    std::unique_ptr<rocksdb::Transaction> Txn(DB->BeginTransaction({}, {}));
    rocksdb::PinnableSlice Fetched;
    if (checkFound(Txn->GetForUpdate({}, HeadsFamily, Head.Name, &Fetched)))
      deleteRef(*Txn, TYPE_HEAD, Head.Name, Fetched);
    Txn->Delete(HeadsFamily, Head.Name);
    TxnStatus = Txn->Commit();
  } while (TxnStatus.IsBusy() || TxnStatus.IsTryAgain());
  checkStatus(TxnStatus);
}

void RocksDBStore::call_invalidate(llvm::StringRef name) {
  std::vector<std::uint8_t> Prefix;
  Node(utf8_string_arg, name).save_cbor(Prefix);
  std::unique_ptr<rocksdb::Iterator> Iterator(DB->NewIterator({}, CallsFamily));
  for (Iterator->Seek(makeSlice(Prefix)); Iterator->Valid(); Iterator->Next()) {
    if (!makeBytes(Iterator->key()).take_front(Prefix.size()).equals(Prefix))
      break;
    rocksdb::Status TxnStatus;
    do {
      std::unique_ptr<rocksdb::Transaction> Txn(DB->BeginTransaction({}, {}));
      Txn->Delete(CallsFamily, Iterator->key());
      deleteRef(*Txn, TYPE_CALL, Iterator->key(), Iterator->value());
      auto Bytes = makeBytes(Iterator->key()).drop_front(Prefix.size());
      while (!Bytes.empty()) {
        auto Arg = CID::loadFromSequence(Bytes);
        deleteRef(*Txn, TYPE_CALL, Iterator->key(), makeSlice(Arg->asBytes()));
      }
      TxnStatus = Txn->Commit();
    } while (TxnStatus.IsBusy() || TxnStatus.IsTryAgain());
    checkStatus(TxnStatus);
  }
  checkStatus(Iterator->status());
}

std::unique_ptr<Store> memodb_rocksdb_open(llvm::StringRef path,
                                           bool create_if_missing) {
  auto db = std::make_unique<RocksDBStore>();
  db->open(path, create_if_missing);
  return db;
}

#else // BCDB_WITH_ROCKSDB

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <memory>

using namespace memodb;

std::unique_ptr<Store> memodb_rocksdb_open(llvm::StringRef path,
                                           bool create_if_missing) {
  llvm::report_fatal_error("MemoDB was compiled without RocksDB support");
}

#endif // BCDB_WITH_ROCKSDB
