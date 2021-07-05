#include "memodb/CAR.h"
#include "memodb_internal.h"

#include <cstdint>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <unistd.h>

#include "memodb/Multibase.h"

using namespace memodb;

namespace {
class CARStore : public Store {
  llvm::sys::fs::file_t FileHandle;
  Node Root;
  std::map<CID, std::uint64_t> BlockPositions;

  bool readBytes(llvm::MutableArrayRef<std::uint8_t> Buf, std::uint64_t *Pos);
  std::optional<std::uint64_t> readVarInt(std::uint64_t *Pos);
  CID readCID(std::uint64_t *Pos);
  Node readValue(std::uint64_t *Pos, std::uint64_t Size);

public:
  void open(llvm::StringRef uri, bool create_if_missing);
  ~CARStore() override;

  llvm::Optional<Node> getOptional(const CID &CID) override;
  llvm::Optional<CID> resolveOptional(const Name &Name) override;
  std::vector<Name> list_names_using(const CID &ref) override;
  std::vector<std::string> list_funcs() override;
  void eachHead(std::function<bool(const Head &)> F) override;
  void eachCall(llvm::StringRef Func,
                std::function<bool(const Call &)> F) override;

  CID put(const Node &value) override;
  void set(const Name &Name, const CID &ref) override;
  void head_delete(const Head &Head) override;
  void call_invalidate(llvm::StringRef name) override;
};
} // end anonymous namespace

bool CARStore::readBytes(llvm::MutableArrayRef<std::uint8_t> Buf,
                         std::uint64_t *Pos) {
  // We need to use pread() or equivalent because there may be multiple threads
  // accessing the CARStore at once.
  while (!Buf.empty()) {
    llvm::ExitOnError Err("readNativeFileSlice: ");
    size_t RC = Err(llvm::sys::fs::readNativeFileSlice(
        FileHandle,
        llvm::MutableArrayRef(reinterpret_cast<char *>(Buf.data()), Buf.size()),
        *Pos));
    if (RC == 0)
      return false;
    Buf = Buf.drop_front(RC);
    *Pos += RC;
  }
  return true;
}

std::optional<std::uint64_t> CARStore::readVarInt(std::uint64_t *Pos) {
  std::uint64_t Result = 0;
  for (unsigned Shift = 0; true; Shift += 7) {
    if (Shift >= 64 - 7)
      llvm::report_fatal_error("VarInt too large");
    std::uint8_t Byte;
    if (!readBytes(Byte, Pos)) {
      if (Shift)
        llvm::report_fatal_error("Unexpected end of file in VarInt");
      return {};
    }
    Result |= (std::uint64_t)(Byte & 0x7f) << Shift;
    if (!(Byte & 0x80)) {
      if (!Byte && Shift)
        llvm::report_fatal_error("VarInt has extra bytes");
      break;
    }
  }
  return Result;
}

CID CARStore::readCID(std::uint64_t *Pos) {
  std::uint64_t StartPos = *Pos;
  auto CIDVersion = *readVarInt(Pos);
  if (CIDVersion != 1)
    llvm::report_fatal_error("Unsupported CID version");
  *readVarInt(Pos);
  *readVarInt(Pos);
  auto HashSize = *readVarInt(Pos);
  *Pos += HashSize;
  std::vector<std::uint8_t> Buffer(*Pos - StartPos);
  if (!readBytes(Buffer, &StartPos))
    llvm::report_fatal_error("Unexpected end of file in CID");
  auto CID = CID::fromBytes(Buffer);
  if (!CID)
    llvm::report_fatal_error("Invalid CID");
  return *CID;
}

Node CARStore::readValue(std::uint64_t *Pos, std::uint64_t Size) {
  std::vector<std::uint8_t> Buf(Size);
  if (!readBytes(Buf, Pos))
    llvm::report_fatal_error("Unexpected end of file in value");
  return llvm::cantFail(Node::loadFromCBOR(Buf));
}

void CARStore::open(llvm::StringRef uri, bool create_if_missing) {
  llvm::ExitOnError Err("CARStore::open: ");
  auto Parsed = URI::parse(uri);
  if (!Parsed || Parsed->scheme != "car" || !Parsed->authority.empty() ||
      !Parsed->query_params.empty() || !Parsed->fragment.empty())
    llvm::report_fatal_error("Unsupported CAR URI");
  FileHandle = Err(llvm::sys::fs::openNativeFile(
      *Parsed->getPathString(), llvm::sys::fs::CD_OpenExisting,
      llvm::sys::fs::FA_Read, llvm::sys::fs::OF_None));

  std::uint64_t Pos = 0;
  auto HeaderSize = *readVarInt(&Pos);
  auto Header = readValue(&Pos, HeaderSize);
  if (Header["version"] != 1 || Header["roots"].size() != 1)
    llvm::report_fatal_error("Unsupported CAR header");
  CID RootRef = Header["roots"][0].as<CID>();

  while (true) {
    auto BlockStart = Pos;
    auto BlockSize = readVarInt(&Pos);
    if (!BlockSize)
      break;
    auto BlockEnd = Pos + *BlockSize;
    CID CID = readCID(&Pos);
    if (Pos > BlockEnd)
      llvm::report_fatal_error("Invalid size of block");
    BlockPositions[std::move(CID)] = BlockStart;
    Pos = BlockEnd;
  }

  Root = get(RootRef);
  if (Root["format"] != "MemoDB CAR" || Root["version"] != 0)
    llvm::report_fatal_error("Unsupported MemoDB CAR version");
}

CARStore::~CARStore() {
  // ignore errors
  llvm::sys::fs::closeFile(FileHandle);
}

llvm::Optional<Node> CARStore::getOptional(const CID &CID) {
  if (CID.isIdentity())
    return llvm::cantFail(Node::loadFromIPLD(CID, {}));
  if (!BlockPositions.count(CID))
    return {};
  auto Pos = BlockPositions[CID];
  auto BlockSize = *readVarInt(&Pos);
  auto BlockEnd = Pos + BlockSize;
  ::CID CIDFromFile = readCID(&Pos);
  if (CID != CIDFromFile)
    llvm::report_fatal_error("CID mismatch (file changed while reading?)");
  std::vector<std::uint8_t> Buffer(BlockEnd - Pos);
  if (!readBytes(Buffer, &Pos))
    llvm::report_fatal_error("Unexpected end of file in content");
  return llvm::cantFail(Node::loadFromIPLD(CID, Buffer));
}

llvm::Optional<memodb::CID> CARStore::resolveOptional(const Name &Name) {
  if (const CID *CIDValue = std::get_if<CID>(&Name)) {
    return *CIDValue;
  } else if (const Head *head = std::get_if<Head>(&Name)) {
    const Node &Value = Root["heads"].at_or_null(head->Name);
    if (Value.is_null())
      return {};
    return Value.as<CID>();
  } else if (const Call *call = std::get_if<Call>(&Name)) {
    const Node &AllCalls = Root["calls"].at_or_null(call->Name);
    if (AllCalls.is_null())
      return {};
    const Node *Value = nullptr;
    Multibase::eachBase([&](const Multibase &multibase) {
      std::string Key;
      for (const CID &Arg : call->Args)
        Key += Arg.asString(multibase) + "/";
      Key.pop_back();
      const Node &v = AllCalls.at_or_null(Key);
      if (!v.is_null())
        Value = &v;
    });
    if (!Value)
      return {};
    return (*Value)["result"].as<CID>();
  } else {
    llvm_unreachable("impossible Name type");
  }
}

std::vector<Name> CARStore::list_names_using(const CID &ref) {
  // No easy way to find references, so return nothing. This function isn't
  // required to find every reference anyway.
  return {};
}

void CARStore::eachCall(llvm::StringRef Func,
                        std::function<bool(const Call &)> F) {
  const Node &Calls = Root["calls"].at_or_null(Func);
  if (Calls.is_null())
    return;
  for (const auto &Item : Calls.map_range()) {
    Call Call(Func, {});
    for (const Node &Arg : Item.value()["args"].list_range())
      Call.Args.emplace_back(Arg.as<CID>());
    if (F(Call))
      break;
  }
}

std::vector<std::string> CARStore::list_funcs() {
  std::vector<std::string> Result;
  for (const auto &Item : Root["calls"].map_range())
    Result.emplace_back(Item.key().str());
  return Result;
}

void CARStore::eachHead(std::function<bool(const Head &)> F) {
  for (const auto &Item : Root["heads"].map_range())
    if (F(Head(Item.key())))
      break;
}

CID CARStore::put(const Node &value) {
  llvm::report_fatal_error("CAR stores are read-only");
}

void CARStore::set(const Name &Name, const CID &ref) {
  llvm::report_fatal_error("CAR stores are read-only");
}

void CARStore::head_delete(const Head &Head) {
  llvm::report_fatal_error("CAR stores are read-only");
}

void CARStore::call_invalidate(llvm::StringRef name) {
  llvm::report_fatal_error("CAR stores are read-only");
}

std::unique_ptr<Store> memodb_car_open(llvm::StringRef path,
                                       bool create_if_missing) {
  auto db = std::make_unique<CARStore>();
  db->open(path, create_if_missing);
  return db;
}

CID memodb::exportToCARFile(llvm::raw_fd_ostream &os, Store &store,
                            llvm::ArrayRef<Name> names_to_export) {
  // Create a CAR file:
  // https://github.com/ipld/specs/blob/master/block-layer/content-addressable-archives.md

  if (!os.supportsSeeking()) {
    llvm::report_fatal_error("output file doesn't support seeking");
  }

  auto getVarIntSize = [&](size_t Value) {
    size_t Total = 1;
    for (; Value >= 0x80; Value >>= 7)
      Total++;
    return Total;
  };
  auto writeVarInt = [&](size_t Value) {
    for (; Value >= 0x80; Value >>= 7)
      os.write((Value & 0x7f) | 0x80);
    os.write(Value);
  };
  auto getBlockSize =
      [&](const std::pair<CID, std::vector<std::uint8_t>> &Block) {
        size_t Result = Block.second.size() + Block.first.asBytes().size();
        Result += getVarIntSize(Result);
        return Result;
      };
  auto writeBlock =
      [&](const std::pair<CID, std::vector<std::uint8_t>> &Block) {
        std::vector<std::uint8_t> Buffer(Block.first.asBytes());
        Buffer.insert(Buffer.end(), Block.second.begin(), Block.second.end());
        writeVarInt(Buffer.size());
        os.write(reinterpret_cast<const char *>(Buffer.data()), Buffer.size());
      };

  // We won't know what root CID to put in the header until after we've written
  // everything. Leave an empty space which will be filled with header +
  // padding later. The header is normally 0x3d bytes, so this gives us plenty
  // of room.
  os.write_zeros(0x200);
  auto DataStartPos = os.tell();

  std::set<CID> AlreadyWritten;
  std::function<void(const CID &)> exportRef;
  std::function<CID(const Node &)> exportValue;

  exportRef = [&](const CID &Ref) {
    if (AlreadyWritten.insert(Ref).second)
      exportValue(store.get(Ref));
  };

  exportValue = [&](const Node &Value) {
    auto Block = Value.saveAsIPLD();
    if (!Block.first.isIdentity())
      writeBlock(Block);
    Value.eachLink(exportRef);
    return Block.first;
  };

  Node Root = Node(node_map_arg, {{"format", "MemoDB CAR"},
                                  {"version", 0},
                                  {"calls", Node(node_map_arg)},
                                  {"heads", Node(node_map_arg)},
                                  {"ids", Node(node_list_arg)}});
  Node &Calls = Root["calls"];
  Node &Heads = Root["heads"];
  Node &IDs = Root["ids"];
  auto addName = [&](const Name &Name) {
    llvm::errs() << "exporting " << Name << "\n";
    if (const CID *Ref = std::get_if<CID>(&Name)) {
      exportRef(*Ref);
      IDs.emplace_back(*Ref);
    } else if (const Head *head = std::get_if<Head>(&Name)) {
      Heads[head->Name] = store.resolve(Name);
      exportRef(Heads[head->Name].as<CID>());
    } else if (const Call *call = std::get_if<Call>(&Name)) {
      Node &FuncCalls = Calls[call->Name];
      if (FuncCalls == Node{})
        FuncCalls = Node::Map();
      Node Args = Node(node_list_arg);
      std::string Key;
      for (const CID &Arg : call->Args) {
        exportRef(Arg);
        Args.emplace_back(Arg);
        Key += std::string(Arg) + "/";
      }
      Key.pop_back();
      auto Result = store.resolve(Name);
      FuncCalls[Key] = Node::Map({{"args", Args}, {"result", Result}});
      exportRef(Result);
    } else {
      llvm_unreachable("impossible Name type");
    }
  };
  if (!names_to_export.empty()) {
    for (const Name &name : names_to_export)
      addName(name);
  } else {
    store.eachHead([&](const Head &head) {
      addName(head);
      return false;
    });
    for (llvm::StringRef Func : store.list_funcs()) {
      store.eachCall(Func, [&](const Call &call) {
        addName(call);
        return false;
      });
    }
  }

  CID RootRef = exportValue(Root);

  Node Header =
      Node::Map({{"roots", Node(node_list_arg, {RootRef})}, {"version", 1}});
  std::vector<std::uint8_t> Buffer;
  Header.save_cbor(Buffer);
  os.seek(0);
  writeVarInt(Buffer.size());
  os.write(reinterpret_cast<const char *>(Buffer.data()), Buffer.size());

  // Add padding between the header and the real data. This code can only
  // generate padding of size 40...128 or 130... bytes, because of the way
  // VarInts work. Note that the padding block may be a duplicate of another
  // block later in the file; the CAR format does not specify whether this is
  // allowed.
  if (os.tell() < DataStartPos) {
    size_t PaddingNeeded = DataStartPos - os.tell();
    std::vector<std::uint8_t> Padding(PaddingNeeded);
    for (size_t i = 0; i < PaddingNeeded; i++)
      Padding[i] = "MemoDB CAR"[i % 11];

    for (ssize_t Size = PaddingNeeded; Size >= 0; Size--) {
      auto Block =
          Node(llvm::ArrayRef(Padding).take_front(Size)).saveAsIPLD(true);
      if (getBlockSize(Block) == PaddingNeeded) {
        writeBlock(Block);
        break;
      }
    }
  }
  if (os.tell() != DataStartPos)
    llvm::report_fatal_error("CAR header too large to fit");

  return RootRef;
}
