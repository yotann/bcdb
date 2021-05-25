#include "memodb_internal.h"

#if BCDB_WITH_LEVELDB

#include <array>
#include <cstdint>
#include <cstdlib>
#include <leveldb/db.h>
#include <leveldb/filter_policy.h>
#include <leveldb/options.h>
#include <leveldb/write_batch.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/StringRef.h>
#include <memory>
#include <sodium.h>
#include <string>
#include <tuple>
#include <vector>

typedef std::array<uint8_t, crypto_generichash_BYTES> Hash;

typedef std::array<uint8_t, 3> KeyType;

static const Hash HASH_NONE = {};

static const KeyType KEY_CALL = {0x01, 0x01, 0x6b};
static const KeyType KEY_CBOR = {0x01, 0x05, 0xd1};
static const KeyType KEY_FUNC = {0x02, 0xd1, 0xa2};
static const KeyType KEY_HEAD = {0x03, 0x90, 0x03};
static const KeyType KEY_REF = {0x08, 0x90, 0xb1};
static const KeyType KEY_RETURN = {0x08, 0x92, 0x6d};

static const leveldb::Slice MAGIC_VALUE("MemoDB v2");

// clang-format off
/* Key types:
 *
 * (empty)                            -> MAGIC_VALUE
 * HASH_NONE + KEY_FUNC   + name      -> (empty)
 * HASH_NONE + KEY_HEAD   + name      -> CBOR ref
 * hash      + KEY_CBOR               -> CBOR value
 * hash      + KEY_REF    + key       -> (empty)
 * name_hash + KEY_CALL   + args_hash -> CBOR [name, args...]
 * name_hash + KEY_RETURN + args_hash -> CBOR ref
 */
// clang-format on

static llvm::ArrayRef<uint8_t> makeBytes(const llvm::StringRef &Str) {
  return llvm::ArrayRef(reinterpret_cast<const uint8_t *>(Str.data()),
                        Str.size());
}

static llvm::ArrayRef<uint8_t> makeBytes(const leveldb::Slice &Slice) {
  return llvm::ArrayRef(reinterpret_cast<const uint8_t *>(Slice.data()),
                        Slice.size());
}

static llvm::ArrayRef<uint8_t> makeBytes(const std::string &Str) {
  return makeBytes(llvm::StringRef(Str));
}

static leveldb::Slice makeSlice(const llvm::ArrayRef<uint8_t> &Bytes) {
  return leveldb::Slice(reinterpret_cast<const char *>(Bytes.data()),
                        Bytes.size());
}

static llvm::StringRef makeStr(const leveldb::Slice &Slice) {
  return llvm::StringRef(Slice.data(), Slice.size());
}

static Hash calculateHash(const llvm::ArrayRef<uint8_t> &Bytes) {
  Hash Result;
  crypto_generichash(Result.data(), Result.size(), Bytes.data(), Bytes.size(),
                     nullptr, 0);
  return Result;
}

namespace {
class LevelDBMemo : public memodb_db {
  std::unique_ptr<const leveldb::FilterPolicy> FilterPolicy;
  std::unique_ptr<leveldb::DB> DB;

  std::tuple<Hash, KeyType, leveldb::Slice> breakKey(const leveldb::Slice &Key);
  void checkStatus(leveldb::Status Status);
  bool checkFound(leveldb::Status Status);
  memodb_ref hashToRef(const Hash &Hash);
  std::string makeKey(const Hash &Hash, const KeyType &KeyType,
                      const leveldb::Slice &Extra = {});
  Hash refToHash(const memodb_ref &Ref);

  void addRefs(leveldb::WriteBatch &Batch, const leveldb::Slice &Key,
               const memodb_value &Value);
  void deleteRefs(leveldb::WriteBatch &Batch, const leveldb::Slice &Key,
                  const memodb_value &Value);

  llvm::Optional<memodb_value> getCBORByKey(const leveldb::Slice &Key);

  memodb_value makeCall(llvm::StringRef Name, llvm::ArrayRef<memodb_ref> Args);

public:
  void open(llvm::StringRef uri, bool create_if_missing);
  ~LevelDBMemo() override;

  llvm::Optional<memodb_value> getOptional(const memodb_name &name) override;
  memodb_ref put(const memodb_value &value) override;
  void set(const memodb_name &Name, const memodb_ref &ref) override;
  std::vector<memodb_name> list_names_using(const memodb_ref &ref) override;
  std::vector<memodb_call> list_calls(llvm::StringRef Func) override;
  std::vector<std::string> list_funcs() override;
  std::vector<memodb_head> list_heads() override;
  void head_delete(const memodb_head &Head) override;

  void call_invalidate(llvm::StringRef name) override;
};
} // end anonymous namespace

std::tuple<Hash, KeyType, leveldb::Slice>
LevelDBMemo::breakKey(const leveldb::Slice &Key) {
  Hash Hash;
  KeyType KeyType;
  leveldb::Slice Extra;
  if (Key.size() < Hash.size() + KeyType.size())
    llvm::report_fatal_error("LevelDB key too small");
  std::memcpy(Hash.data(), Key.data(), Hash.size());
  std::memcpy(KeyType.data(), Key.data() + Hash.size(), KeyType.size());
  Extra = leveldb::Slice(Key.data() + Hash.size() + KeyType.size(),
                         Key.size() - Hash.size() - KeyType.size());
  return std::make_tuple(Hash, KeyType, Extra);
}

void LevelDBMemo::checkStatus(leveldb::Status Status) {
  if (!Status.ok())
    llvm::report_fatal_error("LevelDB error: " + Status.ToString());
}

bool LevelDBMemo::checkFound(leveldb::Status Status) {
  if (Status.IsNotFound())
    return false;
  checkStatus(Status);
  return true;
}

memodb_ref LevelDBMemo::hashToRef(const Hash &Hash) {
  return memodb_ref::fromBlake2BMerkleDAG(Hash);
}

std::string LevelDBMemo::makeKey(const Hash &Hash, const KeyType &KeyType,
                                 const leveldb::Slice &Extra) {
  std::string Result(Hash.size() + KeyType.size() + Extra.size(), '\0');
  std::memcpy(Result.data(), Hash.data(), Hash.size());
  std::memcpy(Result.data() + Hash.size(), KeyType.data(), KeyType.size());
  std::memcpy(Result.data() + Hash.size() + KeyType.size(), Extra.data(),
              Extra.size());
  return Result;
}

Hash LevelDBMemo::refToHash(const memodb_ref &Ref) {
  Hash Result;
  auto Bytes = Ref.asBlake2BMerkleDAG();
  if (Bytes.size() != Result.size())
    llvm::report_fatal_error("invalid hash size");
  for (size_t i = 0; i < Result.size(); i++)
    Result[i] = Bytes[i];
  return Result;
}

void LevelDBMemo::addRefs(leveldb::WriteBatch &Batch, const leveldb::Slice &Key,
                          const memodb_value &Value) {
  if (Value.type() == memodb_value::REF) {
    Hash Dest = refToHash(Value.as_ref());
    std::string NewKey = makeKey(Dest, KEY_REF, Key);
    Batch.Put(NewKey, leveldb::Slice());
  } else if (Value.type() == memodb_value::ARRAY) {
    for (const memodb_value &Item : Value.array_items())
      addRefs(Batch, Key, Item);
  } else if (Value.type() == memodb_value::MAP) {
    for (const auto &Item : Value.map_items()) {
      addRefs(Batch, Key, Item.first);
      addRefs(Batch, Key, Item.second);
    }
  }
}

void LevelDBMemo::deleteRefs(leveldb::WriteBatch &Batch,
                             const leveldb::Slice &Key,
                             const memodb_value &Value) {
  if (Value.type() == memodb_value::REF) {
    Hash Dest = refToHash(Value.as_ref());
    std::string NewKey = makeKey(Dest, KEY_REF, Key);
    Batch.Delete(NewKey);
  } else if (Value.type() == memodb_value::ARRAY) {
    for (const memodb_value &Item : Value.array_items())
      deleteRefs(Batch, Key, Item);
  } else if (Value.type() == memodb_value::MAP) {
    for (const auto &Item : Value.map_items()) {
      deleteRefs(Batch, Key, Item.first);
      deleteRefs(Batch, Key, Item.second);
    }
  }
}

llvm::Optional<memodb_value>
LevelDBMemo::getCBORByKey(const leveldb::Slice &Key) {
  leveldb::ReadOptions ReadOptions;
  std::string Bytes;
  bool Found = checkFound(DB->Get(ReadOptions, Key, &Bytes));
  if (!Found)
    return llvm::None;
  return memodb_value::load_cbor(makeBytes(Bytes));
}

memodb_value LevelDBMemo::makeCall(llvm::StringRef Name,
                                   llvm::ArrayRef<memodb_ref> Args) {
  memodb_value Value = memodb_value::array({memodb_value::string(Name)});
  for (const memodb_ref &Arg : Args)
    Value.array_items().emplace_back(Arg);
  return Value;
}

void LevelDBMemo::open(llvm::StringRef uri, bool create_if_missing) {
  ParsedURI Parsed(uri);
  if (Parsed.Scheme != "leveldb" || !Parsed.Authority.empty() ||
      !Parsed.Query.empty() || !Parsed.Fragment.empty())
    llvm::report_fatal_error("Unsupported LevelDB URI");

  // Keep 10 bits per key in RAM, probably 0.1% the size of the database file.
  FilterPolicy.reset(leveldb::NewBloomFilterPolicy(10));

  leveldb::Options Options;
  Options.create_if_missing = create_if_missing;
  Options.write_buffer_size = 64 * 1024 * 1024;
  Options.block_size = 16 * 1024;
  Options.filter_policy = leveldb::NewBloomFilterPolicy(10);
  leveldb::DB *TmpDB;
  checkStatus(leveldb::DB::Open(Options, Parsed.Path, &TmpDB));
  DB.reset(TmpDB);

  leveldb::ReadOptions ReadOptions;
  std::unique_ptr<leveldb::Iterator> I(DB->NewIterator(ReadOptions));
  I->SeekToFirst();
  if (!I->Valid()) {
    // Empty DB, insert magic value
    leveldb::WriteOptions WriteOptions;
    WriteOptions.sync = true;
    checkStatus(DB->Put(WriteOptions, leveldb::Slice(), MAGIC_VALUE));
  } else {
    // Existing DB, check magic value
    std::string Magic;
    bool Found = checkFound(DB->Get(ReadOptions, leveldb::Slice(), &Magic));
    if (!Found || Magic != MAGIC_VALUE)
      llvm::report_fatal_error("This is the wrong kind of LevelDB data");
  }
}

LevelDBMemo::~LevelDBMemo() {}

llvm::Optional<memodb_value> LevelDBMemo::getOptional(const memodb_name &name) {
  if (const memodb_ref *Ref = std::get_if<memodb_ref>(&name)) {
    std::string Key = makeKey(refToHash(*Ref), KEY_CBOR);
    return getCBORByKey(Key);
  } else if (const memodb_head *Head = std::get_if<memodb_head>(&name)) {
    std::string Key =
        makeKey(HASH_NONE, KEY_HEAD,
                leveldb::Slice(Head->Name.data(), Head->Name.size()));
    return getCBORByKey(Key);
  } else if (const memodb_call *Call = std::get_if<memodb_call>(&name)) {
    Hash NameHash = calculateHash(makeBytes(Call->Name));
    memodb_value Value = makeCall(Call->Name, Call->Args);
    std::vector<std::uint8_t> Buffer;
    Value.save_cbor(Buffer);
    Hash ArgsHash = calculateHash(Buffer);

    std::string Key = makeKey(NameHash, KEY_RETURN, makeSlice(ArgsHash));
    return getCBORByKey(Key);
  } else {
    llvm_unreachable("impossible memodb_name type");
  }
}

memodb_ref LevelDBMemo::put(const memodb_value &value) {
  std::vector<std::uint8_t> Buffer;
  value.save_cbor(Buffer);
  Hash Hash = calculateHash(Buffer);
  std::string Key = makeKey(Hash, KEY_CBOR);
  std::string FetchedValue;
  leveldb::ReadOptions ReadOptions;
  if (checkFound(DB->Get(ReadOptions, Key, &FetchedValue))) {
    assert(makeSlice(Buffer) == FetchedValue);
    return hashToRef(Hash);
  }

  leveldb::WriteBatch Batch;
  Batch.Put(Key, makeSlice(Buffer));
  addRefs(Batch, Key, value);
  leveldb::WriteOptions WriteOptions;
  checkStatus(DB->Write(WriteOptions, &Batch));
  return hashToRef(Hash);
}

void LevelDBMemo::set(const memodb_name &Name, const memodb_ref &ref) {
  leveldb::WriteOptions WriteOptions;
  leveldb::WriteBatch Batch;
  std::vector<std::uint8_t> Buffer;
  if (const memodb_head *Head = std::get_if<memodb_head>(&Name)) {
    std::string Key =
        makeKey(HASH_NONE, KEY_HEAD,
                leveldb::Slice(Head->Name.data(), Head->Name.size()));
    auto OldValue = getCBORByKey(Key);
    // XXX race condition: another thread can change the head at this point, and
    // the ref that thread creates will be left dangling.

    memodb_value value(ref);
    value.save_cbor(Buffer);

    if (OldValue)
      deleteRefs(Batch, Key, *OldValue);
    Batch.Put(Key, makeSlice(Buffer));
    addRefs(Batch, Key, value);
    WriteOptions.sync = true;
  } else if (const memodb_call *Call = std::get_if<memodb_call>(&Name)) {
    // TODO: remove backwards references if we're replacing an older value.
    Hash NameHash = calculateHash(makeBytes(Call->Name));

    std::string Key =
        makeKey(HASH_NONE, KEY_FUNC,
                leveldb::Slice(Call->Name.data(), Call->Name.size()));
    Batch.Put(Key, leveldb::Slice());

    memodb_value Value = makeCall(Call->Name, Call->Args);
    Value.save_cbor(Buffer);
    Hash ArgsHash = calculateHash(Buffer);
    Key = makeKey(NameHash, KEY_CALL, makeSlice(ArgsHash));
    Batch.Put(Key, makeSlice(Buffer));
    addRefs(Batch, Key, Value);

    Buffer.clear();
    Value = ref;
    Value.save_cbor(Buffer);
    Key = makeKey(NameHash, KEY_RETURN, makeSlice(ArgsHash));
    Batch.Put(Key, makeSlice(Buffer));
    addRefs(Batch, Key, Value);
  } else {
    llvm::report_fatal_error("can't set a memodb_ref");
  }
  checkStatus(DB->Write(WriteOptions, &Batch));
}

std::vector<memodb_name> LevelDBMemo::list_names_using(const memodb_ref &ref) {
  std::vector<memodb_name> Result;
  std::string Key = makeKey(refToHash(ref), KEY_REF);
  leveldb::ReadOptions ReadOptions;
  std::unique_ptr<leveldb::Iterator> Iter(DB->NewIterator(ReadOptions));
  for (Iter->Seek(Key); Iter->Valid() && makeStr(Iter->key()).startswith(Key);
       Iter->Next()) {
    leveldb::Slice RefKey = std::get<2>(breakKey(Iter->key()));
    auto RefBroken = breakKey(RefKey);
    auto RefValue = getCBORByKey(RefKey);
    // Stray refs can be left dangling, so double-check that it actually still
    // exists.
    if (!RefValue)
      continue;
    if (std::get<1>(RefBroken) == KEY_CBOR) {
      Result.emplace_back(hashToRef(std::get<0>(RefBroken)));
    } else if (std::get<1>(RefBroken) == KEY_HEAD) {
      Result.emplace_back(memodb_head(std::get<2>(RefBroken).ToString()));
    } else if (std::get<1>(RefBroken) == KEY_CALL ||
               std::get<1>(RefBroken) == KEY_RETURN) {
      if (std::get<1>(RefBroken) == KEY_RETURN) {
        auto CallKey =
            makeKey(std::get<0>(RefBroken), KEY_CALL, std::get<2>(RefBroken));
        RefValue = getCBORByKey(CallKey);
        if (!RefValue)
          continue;
      }
      llvm::StringRef Func = (*RefValue)[0].as_string();
      std::vector<memodb_ref> Args;
      for (const memodb_value &Value :
           llvm::ArrayRef(RefValue->array_items()).drop_front())
        Args.emplace_back(Value.as_ref());
      Result.emplace_back(memodb_call(Func, Args));
    }
  }
  return Result;
}

std::vector<memodb_call> LevelDBMemo::list_calls(llvm::StringRef Func) {
  std::vector<memodb_call> Result;
  Hash NameHash = calculateHash(makeBytes(Func));
  std::string Key = makeKey(NameHash, KEY_CALL);
  leveldb::ReadOptions ReadOptions;
  std::unique_ptr<leveldb::Iterator> Iter(DB->NewIterator(ReadOptions));
  for (Iter->Seek(Key); Iter->Valid() && makeStr(Iter->key()).startswith(Key);
       Iter->Next()) {
    memodb_value RefValue = memodb_value::load_cbor(makeBytes(Iter->value()));
    std::vector<memodb_ref> Args;
    for (const memodb_value &Value :
         llvm::ArrayRef(RefValue.array_items()).drop_front())
      Args.emplace_back(Value.as_ref());
    Result.emplace_back(memodb_call(Func, Args));
  }
  return Result;
}

std::vector<std::string> LevelDBMemo::list_funcs() {
  std::vector<std::string> Result;
  std::string Key = makeKey(HASH_NONE, KEY_FUNC);
  leveldb::ReadOptions ReadOptions;
  std::unique_ptr<leveldb::Iterator> Iter(DB->NewIterator(ReadOptions));
  for (Iter->Seek(Key); Iter->Valid() && makeStr(Iter->key()).startswith(Key);
       Iter->Next()) {
    auto Item = makeStr(Iter->key()).substr(Key.size());
    Result.emplace_back(Item);
  }
  return Result;
}

std::vector<memodb_head> LevelDBMemo::list_heads() {
  std::vector<memodb_head> Result;
  std::string Key = makeKey(HASH_NONE, KEY_HEAD);
  leveldb::ReadOptions ReadOptions;
  std::unique_ptr<leveldb::Iterator> Iter(DB->NewIterator(ReadOptions));
  for (Iter->Seek(Key); Iter->Valid() && makeStr(Iter->key()).startswith(Key);
       Iter->Next()) {
    auto Item = makeStr(Iter->key()).substr(Key.size());
    Result.emplace_back(Item);
  }
  return Result;
}

void LevelDBMemo::head_delete(const memodb_head &Head) {
  std::string Key = makeKey(HASH_NONE, KEY_HEAD,
                            leveldb::Slice(Head.Name.data(), Head.Name.size()));
  auto OldValue = getCBORByKey(Key);
  // XXX race condition: another thread can change the head at this point, and
  // the ref that thread creates will be left dangling.

  leveldb::WriteBatch Batch;
  if (OldValue)
    deleteRefs(Batch, Key, *OldValue);
  Batch.Delete(Key);
  leveldb::WriteOptions WriteOptions;
  WriteOptions.sync = true;
  checkStatus(DB->Write(WriteOptions, &Batch));
}

void LevelDBMemo::call_invalidate(llvm::StringRef name) {
  // TODO: remove backwards references.
  Hash NameHash = calculateHash(makeBytes(name));
  std::string Key = makeSlice(NameHash).ToString();
  leveldb::WriteBatch Batch;
  size_t NumBatched = 0;
  leveldb::ReadOptions ReadOptions;
  leveldb::WriteOptions WriteOptions;
  WriteOptions.sync = true;
  std::unique_ptr<leveldb::Iterator> Iter(DB->NewIterator(ReadOptions));
  for (Iter->Seek(Key); Iter->Valid() && makeStr(Iter->key()).startswith(Key);
       Iter->Next()) {
    Batch.Delete(Iter->key());
    NumBatched++;
    if (NumBatched >= 1 * 1024 * 1024) {
      checkStatus(DB->Write(WriteOptions, &Batch));
      Batch.Clear();
      NumBatched = 0;
    }
  }
  checkStatus(DB->Write(WriteOptions, &Batch));
}

std::unique_ptr<memodb_db> memodb_leveldb_open(llvm::StringRef path,
                                               bool create_if_missing) {
  auto db = std::make_unique<LevelDBMemo>();
  db->open(path, create_if_missing);
  return db;
}

#else // BCDB_WITH_LEVELDB

std::unique_ptr<memodb_db> memodb_leveldb_open(llvm::StringRef path,
                                               bool create_if_missing) {
  llvm::report_fatal_error("LevelDB support was not compiled in");
}

#endif // BCDB_WITH_LEVELDB
