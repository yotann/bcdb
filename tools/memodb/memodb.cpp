#include <cstdlib>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "bcdb/LLVMCompat.h"
#include "memodb/memodb.h"

using namespace llvm;
using namespace memodb;

cl::OptionCategory MemoDBCategory("MemoDB options");

static cl::SubCommand ExportCommand("export", "Export values to a CAR file");
static cl::SubCommand GetCommand("get", "Get a value");
static cl::SubCommand ListCallsCommand("list-calls",
                                       "List all cached calls of a function");
static cl::SubCommand ListFuncsCommand("list-funcs",
                                       "List all cached functions");
static cl::SubCommand ListHeadsCommand("list-heads", "List all heads");
static cl::SubCommand
    PutCommand("put", "Put a value, or find ID of an existing value");
static cl::SubCommand RefsToCommand("refs-to",
                                    "Find names that reference a value");
static cl::SubCommand SetCommand("set", "Set a head or a call result");
static cl::SubCommand TransferCommand("transfer",
                                      "Transfer data to a target database");

static cl::opt<std::string> UriOrEmpty(
    "uri", cl::Optional, cl::desc("URI of the database"),
    cl::init(std::string(StringRef::withNullAsEmpty(std::getenv("BCDB_URI")))),
    cl::cat(MemoDBCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetUri() {
  if (UriOrEmpty.empty()) {
    report_fatal_error(
        "You must provide a database URI, such as sqlite:/tmp/example.bcdb, "
        "using the -uri option or the BCDB_URI environment variable.");
  }
  return UriOrEmpty;
}

// memodb_name options

static cl::opt<std::string> SourceURI(cl::Positional, cl::Required,
                                      cl::desc("<source URI>"),
                                      cl::value_desc("uri"),
                                      cl::cat(MemoDBCategory),
                                      cl::sub(GetCommand));

static cl::opt<std::string>
    TargetURI(cl::Positional, cl::Required, cl::desc("<target URI>"),
              cl::value_desc("uri"), cl::cat(MemoDBCategory),
              cl::sub(RefsToCommand), cl::sub(SetCommand));

static memodb_name GetNameFromURI(llvm::StringRef URI) {
  ParsedURI Parsed(URI);
  if (!Parsed.Authority.empty() || !Parsed.Query.empty() ||
      !Parsed.Fragment.empty())
    report_fatal_error("invalid name URI");
  if (Parsed.Scheme == "head")
    return memodb_head(Parsed.Path);
  else if (Parsed.Scheme == "id")
    return *CID::parse(Parsed.Path);
  else if (Parsed.Scheme == "call") {
    std::vector<CID> Args;
    if (Parsed.PathSegments.empty())
      report_fatal_error("invalid name URI");
    auto FuncName = Parsed.PathSegments.front();
    for (const auto &Arg : llvm::ArrayRef(Parsed.PathSegments).drop_front())
      Args.emplace_back(*CID::parse(Arg));
    return memodb_call(FuncName, Args);
  } else
    report_fatal_error("invalid name URI");
}

// input options (XXX: must come after memodb_name options)

static cl::opt<std::string> InputURI(cl::Positional, cl::desc("<input URI>"),
                                     cl::init("-"), cl::value_desc("uri"),
                                     cl::cat(MemoDBCategory),
                                     cl::sub(PutCommand), cl::sub(SetCommand));

static CID ReadRef(memodb_db &Db, llvm::StringRef URI) {
  ExitOnError Err("value read: ");
  std::unique_ptr<MemoryBuffer> Buffer;
  if (URI == "-")
    Buffer = Err(errorOrToExpected(MemoryBuffer::getSTDIN()));
  else if (llvm::StringRef(URI).startswith("file:")) {
    ParsedURI Parsed(URI);
    if (!Parsed.Authority.empty() || !Parsed.Query.empty() ||
        !Parsed.Fragment.empty())
      report_fatal_error("invalid input URI");
    Buffer = Err(errorOrToExpected(MemoryBuffer::getFile(Parsed.Path)));
  } else {
    memodb_name Name = GetNameFromURI(URI);
    if (CID *Ref = std::get_if<CID>(&Name))
      return *Ref;
    else
      return Db.get(Name).as_ref();
  }
  memodb_value Value = memodb_value::load_cbor(
      {reinterpret_cast<const std::uint8_t *>(Buffer->getBufferStart()),
       Buffer->getBufferSize()});
  return Db.put(Value);
}

// output options

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"),
                           cl::sub(ExportCommand), cl::sub(GetCommand));

static cl::opt<std::string> OutputFilename("o", cl::desc("<output file>"),
                                           cl::init("-"),
                                           cl::value_desc("filename"),
                                           cl::sub(ExportCommand),
                                           cl::sub(GetCommand));

static std::unique_ptr<ToolOutputFile> GetOutputFile() {
  ExitOnError Err("value write: ");
  std::error_code EC;
  auto OutputFile =
      std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::F_None);
  if (EC)
    Err(errorCodeToError(EC));
  if (Force || !CheckBitcodeOutputToConsole(OutputFile->os()))
    return OutputFile;
  return nullptr;
}

static void WriteValue(const memodb_value &Value) {
  auto OutputFile = GetOutputFile();
  if (OutputFile) {
    std::vector<std::uint8_t> Buffer;
    Value.save_cbor(Buffer);
    OutputFile->os().write(reinterpret_cast<const char *>(Buffer.data()),
                           Buffer.size());
    OutputFile->keep();
  }
}

// helper functions

template <typename T> void walkRefs(const memodb_value &Value, T F) {
  if (Value.type() == memodb_value::REF) {
    F(Value.as_ref());
  } else if (Value.type() == memodb_value::ARRAY) {
    for (const memodb_value &Item : Value.array_items())
      walkRefs(Item, F);
  } else if (Value.type() == memodb_value::MAP) {
    for (const auto &Item : Value.map_items())
      walkRefs(Item.second, F);
  }
}

// memodb export

static cl::list<std::string> NamesToExport(cl::Positional, cl::ZeroOrMore,
                                           cl::desc("<names to export>"),
                                           cl::value_desc("names"),
                                           cl::cat(MemoDBCategory),
                                           cl::sub(ExportCommand));

static int Export() {
  // Create a CAR file:
  // https://github.com/ipld/specs/blob/master/block-layer/content-addressable-archives.md

  auto OutputFile = GetOutputFile();
  if (!OutputFile)
    return 1;
  if (!OutputFile->os().supportsSeeking()) {
    errs() << "output file doesn't support seeking\n";
    return 1;
  }

  auto getVarIntSize = [&](size_t Value) {
    size_t Total = 1;
    for (; Value >= 0x80; Value >>= 7)
      Total++;
    return Total;
  };
  auto writeVarInt = [&](size_t Value) {
    for (; Value >= 0x80; Value >>= 7)
      OutputFile->os().write((Value & 0x7f) | 0x80);
    OutputFile->os().write(Value);
  };
  auto getBlockSize = [&](const std::pair<CID, memodb_value::bytes_t> &Block) {
    size_t Result = Block.second.size() + Block.first.asBytes().size();
    Result += getVarIntSize(Result);
    return Result;
  };
  auto writeBlock = [&](const std::pair<CID, memodb_value::bytes_t> &Block) {
    std::vector<std::uint8_t> Buffer(Block.first.asBytes());
    Buffer.insert(Buffer.end(), Block.second.begin(), Block.second.end());
    writeVarInt(Buffer.size());
    OutputFile->os().write(reinterpret_cast<const char *>(Buffer.data()),
                           Buffer.size());
  };

  auto Db = memodb_db_open(GetUri());

  // We won't know what root CID to put in the header until after we've written
  // everything. Leave an empty space which will be filled with header +
  // padding later. The header is normally 0x3d bytes, so this gives us plenty
  // of room.
  OutputFile->os().write_zeros(0x200);
  auto DataStartPos = OutputFile->os().tell();

  std::set<CID> AlreadyWritten;
  std::function<void(const CID &)> exportRef;
  std::function<CID(const memodb_value &)> exportValue;

  exportRef = [&](const CID &Ref) {
    if (AlreadyWritten.insert(Ref).second)
      exportValue(Db->get(Ref));
  };

  exportValue = [&](const memodb_value &Value) {
    auto Block = Value.saveAsIPLD();
    if (!Block.first.isIdentity())
      writeBlock(Block);
    walkRefs(Value, exportRef);
    return Block.first;
  };

  memodb_value Root = memodb_value::map();
  Root["format"] = "MemoDB CAR";
  Root["version"] = 0;
  memodb_value &Calls = Root["calls"] = memodb_value::map();
  memodb_value &Heads = Root["heads"] = memodb_value::map();
  memodb_value &IDs = Root["ids"] = memodb_value::array();
  auto addName = [&](const memodb_name &Name) {
    errs() << "exporting " << Name << "\n";
    if (const CID *Ref = std::get_if<CID>(&Name)) {
      exportRef(*Ref);
      IDs.array_items().emplace_back(*Ref);
    } else if (const memodb_head *Head = std::get_if<memodb_head>(&Name)) {
      Heads[Head->Name] = Db->get(Name);
      exportRef(Heads[Head->Name].as_ref());
    } else if (const memodb_call *Call = std::get_if<memodb_call>(&Name)) {
      memodb_value &FuncCalls = Calls[Call->Name];
      if (FuncCalls == memodb_value{})
        FuncCalls = memodb_value::map();
      memodb_value Args = memodb_value::array();
      std::string Key;
      for (const CID &Arg : Call->Args) {
        exportRef(Arg);
        Args.array_items().emplace_back(Arg);
        Key += std::string(Arg) + "/";
      }
      Key.pop_back();
      auto Result = Db->get(Name);
      FuncCalls[Key] = memodb_value::map({{"args", Args}, {"result", Result}});
      exportRef(Result.as_ref());
    } else {
      llvm_unreachable("impossible memodb_name type");
    }
  };
  if (!NamesToExport.empty()) {
    for (const std::string &NameStr : NamesToExport)
      addName(GetNameFromURI(NameStr));
  } else {
    Db->eachHead([&](const memodb_head &Head) {
      addName(Head);
      return false;
    });
    for (StringRef Func : Db->list_funcs()) {
      Db->eachCall(Func, [&](const memodb_call &Call) {
        addName(Call);
        return false;
      });
    }
  }

  CID RootRef = exportValue(Root);

  memodb_value Header = memodb_value::map(
      {{"roots", memodb_value::array({RootRef})}, {"version", 1}});
  std::vector<std::uint8_t> Buffer;
  Header.save_cbor(Buffer);
  OutputFile->os().seek(0);
  writeVarInt(Buffer.size());
  OutputFile->os().write(reinterpret_cast<const char *>(Buffer.data()),
                         Buffer.size());

  // Add padding between the header and the real data. This code can only
  // generate padding of size 40...128 or 130... bytes, because of the way
  // VarInts work. Note that the padding block may be a duplicate of another
  // block later in the file; the CAR format does not specify whether this is
  // allowed.
  if (OutputFile->os().tell() < DataStartPos) {
    size_t PaddingNeeded = DataStartPos - OutputFile->os().tell();
    std::vector<std::uint8_t> Padding(PaddingNeeded);
    for (size_t i = 0; i < PaddingNeeded; i++)
      Padding[i] = "MemoDB CAR"[i % 11];

    for (ssize_t Size = PaddingNeeded; Size >= 0; Size--) {
      auto Block = memodb_value(llvm::ArrayRef(Padding).take_front(Size))
                       .saveAsIPLD(true);
      if (getBlockSize(Block) == PaddingNeeded) {
        writeBlock(Block);
        break;
      }
    }
  }
  if (OutputFile->os().tell() != DataStartPos)
    llvm::report_fatal_error("CAR header too large to fit");

  OutputFile->keep();
  llvm::errs() << "Exported with Root CID: " << RootRef << "\n";
  return 0;
}

// memodb get

static int Get() {
  auto Name = GetNameFromURI(SourceURI);
  auto Db = memodb_db_open(GetUri());
  auto Value = Db->getOptional(Name);
  if (Value && !std::holds_alternative<CID>(Name))
    Value = Db->getOptional(Value->as_ref());
  if (Value)
    WriteValue(*Value);
  else
    llvm::errs() << "not found\n";
  return 0;
}

// memodb list-calls

static cl::opt<std::string> FuncName(cl::Positional, cl::Required,
                                     cl::desc("<function name>"),
                                     cl::value_desc("func"),
                                     cl::cat(MemoDBCategory),
                                     cl::sub(ListCallsCommand));

static int ListCalls() {
  auto Db = memodb_db_open(GetUri());
  for (const memodb_call &Call : Db->list_calls(FuncName)) {
    outs() << "call:" << Call.Name;
    for (const auto &Arg : Call.Args)
      outs() << "/" << Arg;
    outs() << "\n";
  }
  return 0;
}

// memodb list-funcs

static int ListFuncs() {
  auto Db = memodb_db_open(GetUri());
  for (llvm::StringRef Func : Db->list_funcs())
    outs() << Func << "\n";
  return 0;
}

// memodb list-heads

static int ListHeads() {
  auto Db = memodb_db_open(GetUri());
  for (const memodb_head &Head : Db->list_heads())
    outs() << "head:" << Head.Name << "\n";
  return 0;
}

// memodb put

static int Put() {
  auto Db = memodb_db_open(GetUri());
  CID Ref = ReadRef(*Db, InputURI);
  outs() << "id:" << Ref << "\n";
  return 0;
}

// memodb refs-to

static int RefsTo() {
  auto Db = memodb_db_open(GetUri());
  CID Ref = ReadRef(*Db, TargetURI);
  for (const memodb_name &Name : Db->list_names_using(Ref)) {
    if (auto Head = std::get_if<memodb_head>(&Name))
      outs() << "head:" << Head->Name << "\n";
    else if (auto ParentRef = std::get_if<CID>(&Name))
      outs() << "id:" << *ParentRef << "\n";
    else if (auto Call = std::get_if<memodb_call>(&Name)) {
      outs() << "call:" << Call->Name;
      for (const auto &Arg : Call->Args)
        outs() << "/" << Arg;
      outs() << "\n";
    } else
      llvm_unreachable("impossible value for memodb_name");
  }
  return 0;
}

// memodb set

static int Set() {
  auto Db = memodb_db_open(GetUri());
  auto Name = GetNameFromURI(TargetURI);
  auto Value = ReadRef(*Db, InputURI);
  Db->set(Name, Value);
  return 0;
}

// memodb transfer

static cl::opt<std::string>
    TargetDatabaseURI("target-uri", cl::Required,
                      cl::desc("URI of the target database"),
                      cl::cat(MemoDBCategory), cl::sub(TransferCommand));

static cl::list<std::string> NamesToTransfer(cl::Positional, cl::ZeroOrMore,
                                             cl::desc("<names to transfer>"),
                                             cl::value_desc("names"),
                                             cl::cat(MemoDBCategory),
                                             cl::sub(TransferCommand));

static int Transfer() {
  auto SourceDb = memodb_db_open(GetUri());
  auto TargetDb = memodb_db_open(TargetDatabaseURI);

  std::function<void(const CID &)> transferRef = [&](const CID &Ref) {
    if (!TargetDb->has(Ref)) {
      memodb_value Value = SourceDb->get(Ref);
      TargetDb->put(Value);
      walkRefs(Value, transferRef);
    }
  };

  auto transferName = [&](const memodb_name &Name) {
    errs() << "transferring " << Name << "\n";
    if (const CID *Ref = std::get_if<CID>(&Name)) {
      transferRef(*Ref);
    } else if (const memodb_head *Head = std::get_if<memodb_head>(&Name)) {
      auto Result = SourceDb->get(Name).as_ref();
      transferRef(Result);
      TargetDb->set(*Head, Result);
    } else if (const memodb_call *Call = std::get_if<memodb_call>(&Name)) {
      for (const CID &Arg : Call->Args)
        transferRef(Arg);
      CID Result = SourceDb->get(Name).as_ref();
      transferRef(Result);
      TargetDb->set(*Call, Result);
    } else {
      llvm_unreachable("impossible memodb_name type");
    }
  };

  if (NamesToTransfer.empty()) {
    for (const memodb_head &Head : SourceDb->list_heads())
      transferName(Head);
    for (StringRef Func : SourceDb->list_funcs())
      for (const memodb_call &Call : SourceDb->list_calls(Func))
        transferName(Call);
  } else {
    for (StringRef NameURI : NamesToTransfer)
      transferName(GetNameFromURI(NameURI));
  }
  return 0;
}

// main

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  // Hide LLVM's options, since they're mostly irrelevant.
  bcdb::ReorganizeOptions([](cl::Option *O) {
    if (!bcdb::OptionHasCategory(*O, MemoDBCategory)) {
      O->setHiddenFlag(cl::Hidden);
      O->addSubCommand(*cl::AllSubCommands);
    }
  });

  cl::ParseCommandLineOptions(argc, argv, "MemoDB Tools");

  if (ExportCommand) {
    return Export();
  } else if (GetCommand) {
    return Get();
  } else if (ListCallsCommand) {
    return ListCalls();
  } else if (ListFuncsCommand) {
    return ListFuncs();
  } else if (ListHeadsCommand) {
    return ListHeads();
  } else if (PutCommand) {
    return Put();
  } else if (RefsToCommand) {
    return RefsTo();
  } else if (SetCommand) {
    return Set();
  } else if (TransferCommand) {
    return Transfer();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
