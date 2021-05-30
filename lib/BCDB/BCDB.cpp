#include "bcdb/BCDB.h"

#include <llvm/ADT/SCCIterator.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/iterator_range.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Transforms/IPO.h>
#include <string>
#include <vector>

#include "Util.h"
#include "bcdb/AlignBitcode.h"
#include "bcdb/Split.h"
#include "memodb/memodb.h"

using namespace bcdb;
using namespace llvm;

cl::OptionCategory bcdb::BCDBCategory("BCDB options");

static cl::opt<bool> NoRenameConstants(
    "no-rename-constants",
    cl::desc("Don't improve deduplication by renaming anonymous constants"),
    cl::cat(BCDBCategory));

static cl::opt<bool> RenameGlobals(
    "rename-globals",
    cl::desc("When adding a module, rename referenced globals based on IDs"),
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
    : Context(new LLVMContext()), unique_db(std::move(db)),
      db(unique_db.get()) {}

BCDB::BCDB(memodb_db &db) : Context(new LLVMContext()), db(&db) {}

BCDB::~BCDB() {}

Expected<std::vector<std::string>> BCDB::ListModules() {
  std::vector<std::string> Result;
  for (memodb_head &Head : db->list_heads())
    Result.emplace_back(std::move(Head.Name));
  return Result;
}

Expected<std::vector<std::string>> BCDB::ListFunctionsInModule(StringRef Name) {
  CID ref = db->head_get(Name);
  if (!ref)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  memodb_value head = db->get(ref);
  std::vector<std::string> result;
  for (auto &item : head["functions"].map_items()) {
    result.push_back(std::string(llvm::StringRef(item.second.as_ref())));
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
  db->head_delete(memodb_head(Name));
  return Error::success();
}

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

Expected<CID> BCDB::AddWithoutHead(std::unique_ptr<Module> M) {
  PreprocessModule(*M);

  auto SaveModule = [&](Module &M) {
    SmallVector<char, 0> Buffer;
    WriteAlignedModule(M, Buffer);
    auto value = memodb_value(ArrayRef<uint8_t>(
        reinterpret_cast<uint8_t *>(Buffer.data()), Buffer.size()));
    return db->put(value);
  };

  memodb_value function_map = memodb_value::map();
  Splitter Splitter(*M);

  GlobalReferenceGraph Graph(*M);
  for (auto &SCC : make_range(scc_begin(&Graph), scc_end(&Graph))) {
    DenseMap<GlobalObject *, CID> Map;
    for (auto &Node : SCC) {
      if (GlobalObject *GO = dyn_cast_or_null<GlobalObject>(Node.second)) {
        auto MPart = Splitter.SplitGlobal(GO);
        if (MPart)
          Map[GO] = SaveModule(*MPart);
      }
    }
    for (auto &Item : Map) {
      GlobalObject *GO = Item.first;
      CID &Ref = Item.second;
      function_map[bytesToUTF8(GO->getName())] = Ref;
      if (RenameGlobals) {
        GlobalAlias *GA = GlobalAlias::create(
            GlobalValue::InternalLinkage, "__bcdb_alias_" + StringRef(Ref), GO);
        GO->replaceAllUsesWith(GA);
        GA->setAliasee(GO); // Aliasee was changed by replaceAllUsesWith.
      }
    }
  }

  Splitter.Finish();
  CID remainder_value = SaveModule(*M);

  auto result = memodb_value::map(
      {{"functions", function_map}, {"remainder", remainder_value}});
  return db->put(result);
}

Error BCDB::Add(StringRef Name, std::unique_ptr<Module> M) {
  Expected<CID> refOrErr = AddWithoutHead(std::move(M));
  if (!refOrErr)
    return refOrErr.takeError();
  db->head_set(Name, *refOrErr);
  return Error::success();
}

static std::unique_ptr<Module> LoadModuleFromValue(memodb_db *db,
                                                   const CID &ref,
                                                   StringRef Name,
                                                   LLVMContext &Context) {
  memodb_value value = db->get(ref);
  StringRef buffer_ref(reinterpret_cast<const char *>(value.as_bytes().data()),
                       value.as_bytes().size());
  ExitOnError Err("LoadModuleFromValue");
  return Err(parseBitcodeFile(MemoryBufferRef(buffer_ref, Name), Context));
}

Expected<std::unique_ptr<Module>>
BCDB::LoadParts(StringRef Name, std::map<std::string, std::string> &PartIDs) {
  CID head_ref = db->head_get(Name);
  if (!head_ref)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  memodb_value head = db->get(head_ref);
  auto Remainder =
      LoadModuleFromValue(db, head["remainder"].as_ref(), Name, *Context);

  for (auto &Item : head["functions"].map_items()) {
    auto Name = utf8ToByteString(Item.first);
    CID ref = Item.second.as_ref();
    PartIDs[std::string(Name)] = llvm::StringRef(ref);
  }

  return Remainder;
}

Expected<std::unique_ptr<Module>> BCDB::GetFunctionById(StringRef Id) {
  return LoadModuleFromValue(db, CID(Id), Id, *Context);
}

Expected<std::unique_ptr<Module>> BCDB::Get(StringRef Name) {
  CID head_ref = db->head_get(Name);
  if (!head_ref)
    return make_error<StringError>("could not get head",
                                   inconvertibleErrorCode());
  memodb_value head = db->get(head_ref);

  auto M = LoadModuleFromValue(db, head["remainder"].as_ref(), "remainder",
                               *Context);
  Joiner Joiner(*M);
  for (auto &Item : head["functions"].map_items()) {
    auto Name = utf8ToByteString(Item.first);
    auto MPart = LoadModuleFromValue(db, Item.second.as_ref(), Name, *Context);
    Joiner.JoinGlobal(Name, std::move(MPart));
  }

  Joiner.Finish();
  return M;
}
