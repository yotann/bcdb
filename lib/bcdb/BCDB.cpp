#include "bcdb/BCDB.h"

#include <cstdint>
#include <llvm/ADT/ArrayRef.h>
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

#include "bcdb/AlignBitcode.h"
#include "bcdb/Split.h"
#include "bcdb/Util.h"
#include "memodb/Store.h"

using namespace bcdb;
using namespace llvm;
using namespace memodb;

cl::OptionCategory bcdb::BCDBCategory("BCDB options");

static cl::opt<bool> NoRenameConstants(
    "no-rename-constants",
    cl::desc("Don't improve deduplication by renaming anonymous constants"),
    cl::cat(BCDBCategory));

static cl::opt<bool> RenameGlobals(
    "rename-globals",
    cl::desc("When adding a module, rename referenced globals based on IDs"),
    cl::cat(BCDBCategory));

std::string bcdb::bytesToUTF8(llvm::ArrayRef<std::uint8_t> Bytes) {
  std::string Result;
  for (std::uint8_t Byte : Bytes) {
    if (Byte < 0x80) {
      Result.push_back(static_cast<char>(Byte));
    } else {
      Result.push_back(static_cast<char>(0xc0 | (Byte >> 6)));
      Result.push_back(static_cast<char>(0x80 | (Byte & 0x3f)));
    }
  }
  return Result;
}

std::string bcdb::bytesToUTF8(StringRef Bytes) {
  return bytesToUTF8(llvm::ArrayRef(
      reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
}

std::string bcdb::utf8ToByteString(StringRef Str) {
  std::string Result;
  while (!Str.empty()) {
    std::uint8_t x = (std::uint8_t)Str[0];
    if (x < 0x80) {
      Result.push_back(static_cast<char>(x));
      Str = Str.drop_front(1);
    } else {
      std::uint8_t y = Str.size() >= 2 ? (std::uint8_t)Str[1] : 0;
      if ((x & 0xfc) != 0xc0 || (y & 0xc0) != 0x80)
        llvm::report_fatal_error("invalid UTF-8 bytes");
      Result.push_back(static_cast<char>((x & 3) << 6 | (y & 0x3f)));
      Str = Str.drop_front(2);
    }
  }
  return Result;
}

Error BCDB::Init(StringRef store_uri) {
  Expected<std::unique_ptr<Store>> db =
      Store::open(store_uri, /*create_if_missing*/ true);
  return db.takeError();
}

Expected<std::unique_ptr<BCDB>> BCDB::Open(StringRef store_uri) {
  Expected<std::unique_ptr<Store>> db = Store::open(store_uri);
  if (!db)
    return db.takeError();
  return std::make_unique<BCDB>(std::move(*db));
}

BCDB::BCDB(std::unique_ptr<Store> db)
    : Context(new LLVMContext()), unique_db(std::move(db)),
      db(unique_db.get()) {}

BCDB::BCDB(Store &db) : Context(new LLVMContext()), db(&db) {}

BCDB::~BCDB() {}

Expected<std::vector<std::string>> BCDB::ListModules() {
  std::vector<std::string> Result;
  for (Head &Head : db->list_heads())
    Result.emplace_back(std::move(Head.Name));
  return Result;
}

Expected<std::vector<std::string>> BCDB::ListFunctionsInModule(StringRef Name) {
  CID ref = db->resolve(Head(Name));
  Node head = db->get(ref);
  std::vector<std::string> result;
  for (auto &item : head["functions"].map_range()) {
    result.push_back(std::string(item.value().as<CID>()));
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
  db->head_delete(Head(Name));
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
    std::unique_ptr<ModulePass> CMP(createConstantMergePass());
    CMP->runOnModule(M);
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
    return db->put(Node(byte_string_arg, Buffer));
  };

  Node function_map = Node::Map();
  Splitter Splitter(*M);

  GlobalReferenceGraph Graph(*M);
  for (auto &SCC : make_range(scc_begin(&Graph), scc_end(&Graph))) {
    DenseMap<GlobalObject *, CID> Map;
    for (auto &Node : SCC) {
      if (GlobalObject *GO = dyn_cast_or_null<GlobalObject>(Node.second)) {
        auto MPart = Splitter.SplitGlobal(GO);
        if (MPart)
          Map.insert(std::make_pair(GO, SaveModule(*MPart)));
      }
    }
    for (auto &Item : Map) {
      GlobalObject *GO = Item.first;
      CID &Ref = Item.second;
      function_map[bytesToUTF8(GO->getName())] = Node(Ref);
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

  auto result = Node::Map(
      {{"functions", function_map}, {"remainder", Node(remainder_value)}});
  return db->put(result);
}

Error BCDB::Add(StringRef Name, std::unique_ptr<Module> M) {
  Expected<CID> refOrErr = AddWithoutHead(std::move(M));
  if (!refOrErr)
    return refOrErr.takeError();
  db->set(Head(Name), *refOrErr);
  return Error::success();
}

static std::unique_ptr<Module> LoadModuleFromValue(Store *db, const CID &ref,
                                                   StringRef Name,
                                                   LLVMContext &Context) {
  Node value = db->get(ref);
  ExitOnError Err("LoadModuleFromValue: ");
  return Err(parseBitcodeFile(
      MemoryBufferRef(value.as<StringRef>(byte_string_arg), Name), Context));
}

Expected<std::unique_ptr<Module>>
BCDB::LoadParts(StringRef Name, std::map<std::string, std::string> &PartIDs) {
  CID head_ref = db->resolve(Head(Name));
  Node head = db->get(head_ref);
  auto Remainder =
      LoadModuleFromValue(db, head["remainder"].as<CID>(), Name, *Context);

  for (auto &Item : head["functions"].map_range()) {
    auto Name = utf8ToByteString(Item.key());
    CID ref = Item.value().as<CID>();
    PartIDs[std::string(Name)] = llvm::StringRef(ref);
  }

  return Remainder;
}

Expected<std::unique_ptr<Module>> BCDB::GetFunctionById(StringRef Id) {
  return LoadModuleFromValue(db, *CID::parse(Id), Id, *Context);
}

Expected<std::unique_ptr<Module>> BCDB::Get(StringRef Name) {
  CID head_ref = db->resolve(Head(Name));
  Node head = db->get(head_ref);

  auto M = LoadModuleFromValue(db, head["remainder"].as<CID>(), "remainder",
                               *Context);
  Joiner Joiner(*M);
  for (auto &Item : head["functions"].map_range()) {
    auto Name = utf8ToByteString(Item.key());
    auto MPart =
        LoadModuleFromValue(db, Item.value().as<CID>(), Name, *Context);
    Joiner.JoinGlobal(Name, std::move(MPart));
  }

  Joiner.Finish();
  return M;
}
