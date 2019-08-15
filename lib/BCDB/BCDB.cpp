#include "bcdb/BCDB.h"

#include "bcdb/AlignBitcode.h"
#include "bcdb/Split.h"

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Transforms/IPO.h>
#include <string>
#include <vector>

using namespace bcdb;
using namespace llvm;

static cl::opt<bool> NoRenameConstants(
    "no-rename-constants",
    cl::desc("Don't improve deduplication by renaming anonymous constants"),
    cl::sub(*cl::AllSubCommands));

Error BCDB::Init(StringRef uri) {
  Expected<std::unique_ptr<memodb_db>> db =
      memodb_db_open(uri.str().c_str(), /*create_if_missing*/ true);
  return db.takeError();
}

Expected<std::unique_ptr<BCDB>> BCDB::Open(StringRef uri) {
  Expected<std::unique_ptr<memodb_db>> db = memodb_db_open(uri.str().c_str());
  if (!db)
    return db.takeError();
  return std::make_unique<BCDB>(std::move(*db));
}

BCDB::BCDB(std::unique_ptr<memodb_db> db) : db(std::move(db)) {}

BCDB::~BCDB() {}

Expected<std::vector<std::string>> BCDB::ListModules() {
  return db->list_heads();
}

Expected<std::vector<std::string>> BCDB::ListFunctionsInModule(StringRef Name) {
  memodb_value *head = db->head_get(Name.str().c_str());
  if (!head)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  memodb_value *parent = db->map_lookup(head, "functions");
  delete head;
  if (!parent)
    return make_error<StringError>("could not look up function",
                                   inconvertibleErrorCode());
  auto items = db->map_list_items(parent);
  delete parent;
  std::vector<std::string> result;
  result.reserve(items.size());
  for (auto &item : items) {
    result.push_back(db->value_get_id(item.second.get()));
  }
  return result;
}

Expected<std::vector<std::string>> BCDB::ListAllFunctions() {
  auto modules = ListModules();
  if (!modules)
    return modules.takeError();
  std::vector<std::string> result;
  for (auto &module : *modules) {
    auto functions = ListFunctionsInModule(module);
    if (!functions)
      return functions.takeError();
    result.insert(result.end(), functions->begin(), functions->end());
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

Error BCDB::Delete(llvm::StringRef Name) {
  db->head_delete(Name);
  return Error::success();
}

namespace {
class BCDBSplitSaver : public SplitSaver {
  memodb_db *db;
  std::vector<std::string> function_keys;
  std::vector<std::unique_ptr<memodb_value>> function_values;
  std::unique_ptr<memodb_value> remainder_value;

  Expected<std::unique_ptr<memodb_value>> SaveModule(Module &M) {
    SmallVector<char, 0> Buffer;
    WriteUnalignedModule(M, Buffer);
    return db->blob_create(ArrayRef<uint8_t>(
        reinterpret_cast<uint8_t *>(Buffer.data()), Buffer.size()));
  }

public:
  BCDBSplitSaver(memodb_db *db) : db(db) {}
  Error saveFunction(std::unique_ptr<Module> M, StringRef Name) override {
    Expected<std::unique_ptr<memodb_value>> value = SaveModule(*M);
    if (!value)
      return value.takeError();
    function_keys.push_back(Name);
    function_values.emplace_back(std::move(*value));
    return Error::success();
  }
  Error saveRemainder(std::unique_ptr<Module> M) override {
    Expected<std::unique_ptr<memodb_value>> value = SaveModule(*M);
    if (!value)
      return value.takeError();
    remainder_value = std::move(*value);
    return Error::success();
  }
  Expected<memodb_value *> finish() {
    std::vector<const char *> function_keys_c;
    for (const std::string &x : function_keys)
      function_keys_c.push_back(x.c_str());
    std::vector<memodb_value *> function_values_c;
    for (auto &x : function_values)
      function_values_c.push_back(x.get());
    memodb_value *function_map = db->map_create(
        function_keys_c.data(), function_values_c.data(), function_keys.size());
    if (!function_map)
      return make_error<StringError>("could not create function map",
                                     inconvertibleErrorCode());

    const char *keys[] = {"functions", "remainder"};
    memodb_value *values[] = {function_map, remainder_value.get()};
    memodb_value *result = db->map_create(keys, values, 2);
    delete function_map;
    if (!result)
      return make_error<StringError>("could not create module map",
                                     inconvertibleErrorCode());
    return result;
  }
};
} // end anonymous namespace

static hash_code HashConstant(Constant *C) {
  if (auto *CAZ = dyn_cast<ConstantAggregateZero>(C))
    return hash_value(CAZ->getNumElements());
  if (auto *CDS = dyn_cast<ConstantDataSequential>(C))
    return hash_value(CDS->getRawDataValues());
  return 0;
}

static void RenameAnonymousConstants(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasPrivateLinkage())
      continue;
    if (!GV.hasInitializer())
      continue;
    if (GV.getName() != "str" && !GV.getName().contains('.'))
      continue;

    auto Hash = static_cast<size_t>(HashConstant(GV.getInitializer()));
    if (!Hash)
      continue;
    SmallString<64> TempStr(".sh.");
    raw_svector_ostream TmpStream(TempStr);
    TmpStream << (Hash & 0xffffffff);
    GV.setName(TmpStream.str());
  }
}

static void PreprocessModule(Module &M) {
  if (!NoRenameConstants) {
    createConstantMergePass()->runOnModule(M);
    RenameAnonymousConstants(M);
  }

  // LLVM may output MD kinds inconsistently depending on whether getMDKindID()
  // has been called or not. We call it here to try to make sure output bitcode
  // always includes the same set of MD kinds, improving deduplication.
  M.getMDKindID("srcloc");
}

Error BCDB::Add(StringRef Name, std::unique_ptr<Module> M) {
  PreprocessModule(*M);
  BCDBSplitSaver Saver(db.get());
  if (Error Err = SplitModule(std::move(M), Saver))
    return Err;
  Expected<memodb_value *> ValueOrErr = Saver.finish();
  if (!ValueOrErr)
    return ValueOrErr.takeError();
  db->head_set(Name, *ValueOrErr);
  delete *ValueOrErr;
  return Error::success();
}

static Expected<std::unique_ptr<Module>>
LoadModuleFromValue(memodb_db *db, memodb_value *value, StringRef Name,
                    LLVMContext &Context) {
  size_t buffer_size;
  int rc = db->blob_get_size(value, &buffer_size);
  const char *buffer =
      reinterpret_cast<const char *>(db->blob_get_buffer(value));
  if (rc || !buffer)
    return make_error<StringError>("could not read module blob",
                                   inconvertibleErrorCode());
  StringRef buffer_ref(buffer, buffer_size);
  auto result = parseBitcodeFile(MemoryBufferRef(buffer_ref, Name), Context);
  return result;
}

// FIXME: duplicated from lib/Split/Join.cpp
static bool isStub(const Function &F) {
  if (F.isDeclaration() || F.size() != 1)
    return false;
  const BasicBlock &BB = F.getEntryBlock();
  if (BB.size() != 1 || !isa<UnreachableInst>(BB.front()))
    return false;
  return true;
}

Expected<std::unique_ptr<Module>>
BCDB::LoadParts(StringRef Name, std::map<std::string, std::string> &PartIDs) {
  memodb_value *head = db->head_get(Name.str().c_str());
  if (!head)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());

  memodb_value *remainder = db->map_lookup(head, "remainder");
  memodb_value *functions = db->map_lookup(head, "functions");
  if (!remainder || !functions)
    return make_error<StringError>("could not look up parts of module",
                                   inconvertibleErrorCode());

  auto Remainder = LoadModuleFromValue(db.get(), remainder, Name, Context);
  if (!Remainder)
    return Remainder.takeError();

  for (Function &F : **Remainder) {
    if (isStub(F)) {
      StringRef Name = F.getName();
      memodb_value *value = db->map_lookup(functions, Name.str().c_str());
      if (!value)
        return make_error<StringError>("could not look up function",
                                       inconvertibleErrorCode());
      PartIDs[Name] = db->value_get_id(value);
      delete value;
    }
  }

  delete head;
  delete remainder;
  delete functions;

  return Remainder;
}

Expected<std::unique_ptr<Module>> BCDB::GetFunctionById(StringRef Id) {
  auto value = db->get_value_by_id(Id);
  return LoadModuleFromValue(db.get(), value.get(), Id, Context);
}

namespace {
class BCDBSplitLoader : public SplitLoader {
  LLVMContext &Context;
  memodb_db *db;
  memodb_value *root;

  Expected<std::unique_ptr<Module>> LoadModule(memodb_value *parent,
                                               StringRef Name) {
    memodb_value *value = db->map_lookup(parent, Name.str().c_str());
    if (!value)
      return make_error<StringError>("could not look up module",
                                     inconvertibleErrorCode());
    auto result = LoadModuleFromValue(db, value, Name, Context);
    delete value;
    return result;
  }

public:
  BCDBSplitLoader(LLVMContext &Context, memodb_db *db, memodb_value *root)
      : Context(Context), db(db), root(root) {}
  ~BCDBSplitLoader() { delete root; }

  Expected<std::unique_ptr<Module>> loadFunction(StringRef Name) override {
    memodb_value *parent = db->map_lookup(root, "functions");
    if (!parent)
      return make_error<StringError>("could not look up function",
                                     inconvertibleErrorCode());
    auto result = LoadModule(parent, Name);
    delete parent;
    return result;
  }

  Expected<std::unique_ptr<Module>> loadRemainder() override {
    return LoadModule(root, "remainder");
  }
};
} // end anonymous namespace

Expected<std::unique_ptr<Module>> BCDB::Get(StringRef Name) {
  memodb_value *value = db->head_get(Name.str().c_str());
  if (!value)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  BCDBSplitLoader Loader(Context, db.get(), value);
  return JoinModule(Loader);
}
