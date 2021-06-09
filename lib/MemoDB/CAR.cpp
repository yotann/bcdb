#include "memodb_internal.h"

#include <cstdint>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <map>
#include <memory>
#include <optional>
#include <system_error>
#include <unistd.h>

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
#if LLVM_VERSION_MAJOR >= 10
    llvm::ExitOnError Err("readNativeFileSlice: ");
    size_t RC = Err(llvm::sys::fs::readNativeFileSlice(
        FileHandle,
        llvm::MutableArrayRef(reinterpret_cast<char *>(Buf.data()), Buf.size()),
        *Pos));
#elif defined(LLVM_ON_UNIX)
    ssize_t RC = pread(FileHandle, Buf.data(), Buf.size(), *Pos);
    if (RC == -1) {
      if (errno == EAGAIN)
        continue;
      llvm::report_fatal_error("Error reading file");
    }
#else
#error Function unimplemented for this version of LLVM
#endif
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
  return Node::load_cbor(Buf);
}

void CARStore::open(llvm::StringRef uri, bool create_if_missing) {
  llvm::ExitOnError Err("CARStore::open: ");
  ParsedURI Parsed(uri);
  if (Parsed.Scheme != "car" || !Parsed.Authority.empty() ||
      !Parsed.Query.empty() || !Parsed.Fragment.empty())
    llvm::report_fatal_error("Unsupported CAR URI");
  FileHandle = Err(llvm::sys::fs::openNativeFile(
      Parsed.Path, llvm::sys::fs::CD_OpenExisting, llvm::sys::fs::FA_Read,
      llvm::sys::fs::OF_None));

  std::uint64_t Pos = 0;
  auto HeaderSize = *readVarInt(&Pos);
  auto Header = readValue(&Pos, HeaderSize);
  if (Header["version"] != 1 || Header["roots"].size() != 1)
    llvm::report_fatal_error("Unsupported CAR header");
  CID RootRef = Header["roots"][0].as_link();

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
    return Node::loadFromIPLD(CID, {});
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
  return Node::loadFromIPLD(CID, Buffer);
}

llvm::Optional<memodb::CID> CARStore::resolveOptional(const Name &Name) {
  if (const CID *CIDValue = std::get_if<CID>(&Name)) {
    return *CIDValue;
  } else if (const Head *head = std::get_if<Head>(&Name)) {
    const Node &Value = Root["heads"].at_or_null(head->Name);
    if (Value.is_null())
      return {};
    return Value.as_link();
  } else if (const Call *call = std::get_if<Call>(&Name)) {
    const Node &AllCalls = Root["calls"].at_or_null(call->Name);
    if (AllCalls.is_null())
      return {};
    std::string Key;
    for (const CID &Arg : call->Args)
      Key += std::string(Arg) + "/";
    Key.pop_back();
    const Node &Value = AllCalls.at_or_null(Key);
    if (Value.is_null())
      return {};
    return Value["result"].as_link();
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
      Call.Args.emplace_back(Arg.as_link());
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
