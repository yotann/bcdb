#include "bcdb/BCDB.h"

#include "bcdb/AlignBitcode.h"
#include "bcdb/Split.h"

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Transforms/IPO.h>
#include <string>
#include <vector>

using namespace bcdb;
using namespace llvm;

cl::OptionCategory bcdb::BCDBCategory("BCDB options");

static cl::opt<bool> NoRenameConstants(
    "no-rename-constants",
    cl::desc("Don't improve deduplication by renaming anonymous constants"),
    cl::cat(BCDBCategory));

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

BCDB::BCDB(std::unique_ptr<memodb_db> db)
    : Context(new LLVMContext()), db(std::move(db)) {}

BCDB::~BCDB() {}

Expected<std::vector<std::string>> BCDB::ListModules() {
  return db->list_heads();
}

Expected<std::vector<std::string>> BCDB::ListFunctionsInModule(StringRef Name) {
  memodb_ref ref = db->head_get(Name);
  if (!ref)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  memodb_value head = db->get(ref);
  std::vector<std::string> result;
  for (auto &item : head["functions"].map_items()) {
    result.push_back(llvm::StringRef(item.second.as_ref()));
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
  std::map<std::string, memodb_ref> functions;
  memodb_ref remainder_value;

  Expected<memodb_ref> SaveModule(Module &M) {
    SmallVector<char, 0> Buffer;
    WriteUnalignedModule(M, Buffer);
    auto value = memodb_value(ArrayRef<uint8_t>(
        reinterpret_cast<uint8_t *>(Buffer.data()), Buffer.size()));
    return db->put(value);
  }

public:
  BCDBSplitSaver(memodb_db *db) : db(db) {}
  Error saveFunction(std::unique_ptr<Module> M, StringRef Name) override {
    Expected<memodb_ref> value = SaveModule(*M);
    if (!value)
      return value.takeError();
    functions[Name] = *value;
    return Error::success();
  }
  Error saveRemainder(std::unique_ptr<Module> M) override {
    Expected<memodb_ref> value = SaveModule(*M);
    if (!value)
      return value.takeError();
    remainder_value = *value;
    return Error::success();
  }
  Expected<memodb_ref> finish() {
    auto function_map = memodb_value::map();
    for (auto &item : functions)
      function_map[memodb_value::bytes(item.first)] = item.second;

    auto result = memodb_value::map(
        {{"functions", function_map}, {"remainder", remainder_value}});
    return db->put(result);
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
  Expected<memodb_ref> ValueOrErr = Saver.finish();
  if (!ValueOrErr)
    return ValueOrErr.takeError();
  db->head_set(Name, *ValueOrErr);
  return Error::success();
}

static Expected<std::unique_ptr<Module>>
LoadModuleFromValue(memodb_db *db, const memodb_ref &ref, StringRef Name,
                    LLVMContext &Context) {
  memodb_value value = db->get(ref);
  StringRef buffer_ref(reinterpret_cast<const char *>(value.as_bytes().data()),
                       value.as_bytes().size());
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
  memodb_ref head_ref = db->head_get(Name);
  if (!head_ref)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  memodb_value head = db->get(head_ref);
  auto Remainder =
      LoadModuleFromValue(db.get(), head["remainder"].as_ref(), Name, *Context);
  if (!Remainder)
    return Remainder.takeError();

  for (Function &F : **Remainder) {
    if (isStub(F)) {
      StringRef Name = F.getName();
      memodb_ref ref = head["functions"][memodb_value::bytes(Name)].as_ref();
      if (!ref)
        return make_error<StringError>("could not look up function",
                                       inconvertibleErrorCode());
      PartIDs[Name] = llvm::StringRef(ref);
    }
  }

  return Remainder;
}

Expected<std::unique_ptr<Module>> BCDB::GetFunctionById(StringRef Id) {
  return LoadModuleFromValue(db.get(), memodb_ref(Id), Id, *Context);
}

namespace {
class BCDBSplitLoader : public SplitLoader {
  LLVMContext &Context;
  memodb_db *db;
  memodb_value root;

public:
  BCDBSplitLoader(LLVMContext &Context, memodb_db *db,
                  const memodb_ref &root_ref)
      : Context(Context), db(db), root(db->get(root_ref)) {}

  Expected<std::unique_ptr<Module>> loadFunction(StringRef Name) override {
    return LoadModuleFromValue(
        db, root["functions"][memodb_value::bytes(Name)].as_ref(), Name,
        Context);
  }

  Expected<std::unique_ptr<Module>> loadRemainder() override {
    return LoadModuleFromValue(db, root["remainder"].as_ref(), "remainder",
                               Context);
  }
};
} // end anonymous namespace

Expected<std::unique_ptr<Module>> BCDB::Get(StringRef Name) {
  memodb_ref head = db->head_get(Name);
  if (!head)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  BCDBSplitLoader Loader(*Context, db.get(), head);
  return JoinModule(Loader);
}
