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

typedef std::array<uint8_t, 4> KeyType;

static const Hash HASH_NONE = {};

static const KeyType KEY_CALL = {0xff, 0x09, 0xa9, 0x65};
static const KeyType KEY_CBOR = {0xff, 0x08, 0x13, 0x91};
static const KeyType KEY_FUNC = {0xff, 0x16, 0xe9, 0xdc};
static const KeyType KEY_HEAD = {0xff, 0x1d, 0xe6, 0x9d};
static const KeyType KEY_REF = {0xff, 0x45, 0xe7, 0xff};
static const KeyType KEY_RETURN = {0xff, 0x45, 0xeb, 0x67};

static const leveldb::Slice MAGIC_VALUE("MemoDB v0");

static const llvm::StringRef BASE64_TABLE("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                          "abcdefghijklmnopqrstuvwxyz"
                                          "0123456789+/");

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

static leveldb::Slice makeSlice(const llvm::ArrayRef<uint8_t> &Bytes) {
  return leveldb::Slice(reinterpret_cast<const char *>(Bytes.data()),
                        Bytes.size());
}

static Hash calculateHash(const llvm::ArrayRef<uint8_t> &Bytes) {
  Hash Result;
  crypto_generichash(Result.data(), Result.size(), Bytes.data(), Bytes.size(),
                     nullptr, 0);
  return Result;
}

namespace {
struct ParsedURI {
  ParsedURI(llvm::StringRef URI);

  llvm::StringRef Scheme, Authority, Path, Query, Fragment;
};
} // end anonymous namespace

ParsedURI::ParsedURI(llvm::StringRef URI) {
  std::tie(Scheme, URI) = URI.split(':');
  if (URI.empty())
    std::swap(Scheme, URI);
  if (URI.startswith("//")) {
    size_t i = URI.find_first_of("/?#", 2);
    if (i == llvm::StringRef::npos) {
      Authority = URI;
      URI = "";
    } else {
      Authority = URI.substr(2, i);
      URI = URI.substr(i);
    }
  }
  std::tie(URI, Fragment) = URI.split('#');
  std::tie(Path, Query) = URI.split('?');

  if (Authority.contains('%') || Path.contains('%') || Query.contains('%') ||
      Fragment.contains('%'))
    llvm::report_fatal_error("Percent-encoding in URIs is not supported yet");
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
  // Encode in base64
  auto ResultSize = (Hash.size() + 2) / 3 * 4;
  std::string Result(ResultSize, '\0');
  for (size_t i = 0; i < ResultSize / 4; i++) {
    size_t x = size_t(Hash[3 * i + 0]) << 16;
    if (3 * i + 1 < Hash.size())
      x |= size_t(Hash[3 * i + 1]) << 8;
    if (3 * i + 2 < Hash.size())
      x |= size_t(Hash[3 * i + 2]) << 0;
    Result[4 * i + 0] = BASE64_TABLE[(x >> 18) & 0x3f];
    Result[4 * i + 1] = BASE64_TABLE[(x >> 12) & 0x3f];
    Result[4 * i + 2] =
        3 * i + 1 < Hash.size() ? BASE64_TABLE[(x >> 6) & 0x3f] : '=';
    Result[4 * i + 3] =
        3 * i + 2 < Hash.size() ? BASE64_TABLE[(x >> 0) & 0x3f] : '=';
  }
  return memodb_ref(Result);
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
  // Decode from base64
  llvm::StringRef Str = Ref;
  if (Str.size() % 4)
    llvm::report_fatal_error("invalid base64: wrong size");
  auto Trimmed = Str.rtrim('=');
  auto Padding = Str.size() - Trimmed.size();
  if (Padding > 2)
    llvm::report_fatal_error("invalid base64: too much padding");
  for (char c : Trimmed)
    if (BASE64_TABLE.find(c) == llvm::StringRef::npos)
      llvm::report_fatal_error("invalid base64: invalid character");

  Hash Result;
  auto ResultSize = Str.size() / 4 * 3 - Padding;
  if (ResultSize != Result.size())
    llvm::report_fatal_error("invalid base64: wrong size");

  for (size_t i = 0; i < Str.size() / 4; i++) {
    size_t x0 = BASE64_TABLE.find(Str[4 * i + 0]);
    size_t x1 = BASE64_TABLE.find(Str[4 * i + 1]);
    size_t x2 = Str[4 * i + 2] == '=' ? 0 : BASE64_TABLE.find(Str[4 * i + 2]);
    size_t x3 = Str[4 * i + 3] == '=' ? 0 : BASE64_TABLE.find(Str[4 * i + 3]);
    size_t x = x0 << 18 | x1 << 12 | x2 << 6 | x3;
    Result[3 * i + 0] = x >> 16;
    if (3 * i + 1 < Result.size())
      Result[3 * i + 1] = (x >> 8) & 0xff;
    if (3 * i + 2 < Result.size())
      Result[3 * i + 2] = (x >> 0) & 0xff;
  }
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
  checkStatus(leveldb::DB::Open(Options, Parsed.Path.str(), &TmpDB));
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
  for (Iter->Seek(Key);
       Iter->Valid() && llvm::StringRef(Iter->key().ToString()).startswith(Key);
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
    } else if (std::get<1>(RefBroken) == KEY_CALL) {
      // FIXME
    } else if (std::get<1>(RefBroken) == KEY_RETURN) {
      // FIXME
    }
  }
  return Result;
}

std::vector<memodb_head> LevelDBMemo::list_heads() {
  std::vector<memodb_head> Result;
  std::string Key = makeKey(HASH_NONE, KEY_HEAD);
  leveldb::ReadOptions ReadOptions;
  std::unique_ptr<leveldb::Iterator> Iter(DB->NewIterator(ReadOptions));
  for (Iter->Seek(Key);
       Iter->Valid() && llvm::StringRef(Iter->key().ToString()).startswith(Key);
       Iter->Next()) {
    auto Item = llvm::StringRef(Iter->key().ToString()).substr(Key.size());
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
  for (Iter->Seek(Key);
       Iter->Valid() && llvm::StringRef(Iter->key().ToString()).startswith(Key);
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
