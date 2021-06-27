#include <cstdlib>
#include <iostream>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/InitLLVM.h>
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

#include "memodb/CAR.h"
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"

using namespace llvm;
using namespace memodb;

cl::OptionCategory MemoDBCategory("MemoDB options");

static cl::SubCommand ExportCommand("export", "Export values to a CAR file");
static cl::SubCommand GetCommand("get", "Get a value");
static cl::SubCommand InitCommand("init", "Initialize a store");
static cl::SubCommand InvalidateCommand("invalidate",
                                        "Delete all results of a function");
static cl::SubCommand ListCallsCommand("list-calls",
                                       "List all cached calls of a function");
static cl::SubCommand ListFuncsCommand("list-funcs",
                                       "List all cached functions");
static cl::SubCommand ListHeadsCommand("list-heads", "List all heads");
static cl::SubCommand
    PathsToCommand("paths-to",
                   "Find paths from a head or call that reach a value");
static cl::SubCommand
    PutCommand("put", "Put a value, or find ID of an existing value");
static cl::SubCommand RefsToCommand("refs-to",
                                    "Find names that reference a value");
static cl::SubCommand SetCommand("set", "Set a head or a call result");
static cl::SubCommand TransferCommand("transfer",
                                      "Transfer data to a target database");

static cl::opt<std::string>
    StoreUriOrEmpty("store", cl::Optional, cl::desc("URI of the MemoDB store"),
                    cl::init(std::string(StringRef::withNullAsEmpty(
                        std::getenv("MEMODB_STORE")))),
                    cl::cat(MemoDBCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetStoreUri() {
  if (StoreUriOrEmpty.empty()) {
    report_fatal_error("You must provide a MemoDB store URI, such as "
                       "sqlite:/tmp/example.bcdb, using the -store option or "
                       "the MEMODB_STORE environment variable.");
  }
  return StoreUriOrEmpty;
}

// format options

enum Format {
  Format_CBOR,
  Format_Raw,
  Format_JSON,
};

static cl::opt<Format> format_option(
    "format", cl::Optional, cl::desc("Format for input and output nodes"),
    cl::values(clEnumValN(Format_CBOR, "cbor", "original DAG-CBOR."),
               clEnumValN(Format_Raw, "raw",
                          "raw binary data without CBOR wrapper."),
               clEnumValN(Format_JSON, "json", "MemoDB JSON.")),
    cl::sub(GetCommand), cl::sub(PutCommand), cl::sub(SetCommand));

// Name options

static cl::opt<std::string> SourceURI(cl::Positional, cl::Required,
                                      cl::desc("<source URI>"),
                                      cl::value_desc("uri"),
                                      cl::cat(MemoDBCategory),
                                      cl::sub(GetCommand));

static cl::opt<std::string>
    TargetURI(cl::Positional, cl::Required, cl::desc("<target URI>"),
              cl::value_desc("uri"), cl::cat(MemoDBCategory),
              cl::sub(PathsToCommand), cl::sub(RefsToCommand),
              cl::sub(SetCommand));

static cl::opt<std::string>
    FuncName(cl::Positional, cl::Required, cl::desc("<function name>"),
             cl::value_desc("func"), cl::cat(MemoDBCategory),
             cl::sub(InvalidateCommand), cl::sub(ListCallsCommand));

static Name GetNameFromURI(llvm::StringRef URI) {
  ParsedURI Parsed(URI);
  if (!Parsed.Authority.empty() || !Parsed.Query.empty() ||
      !Parsed.Fragment.empty())
    report_fatal_error("invalid name URI");
  if (Parsed.Scheme == "head") {
    return Head(Parsed.Path);
  } else if (Parsed.Scheme == "id") {
    return *CID::parse(Parsed.Path);
  } else if (Parsed.Scheme == "call") {
    std::vector<CID> Args;
    if (Parsed.PathSegments.empty())
      report_fatal_error("invalid name URI");
    auto FuncName = Parsed.PathSegments.front();
    for (const auto &Arg : llvm::ArrayRef(Parsed.PathSegments).drop_front())
      Args.emplace_back(*CID::parse(Arg));
    return Call(FuncName, Args);
  } else {
    report_fatal_error("invalid name URI");
  }
}

// input options (XXX: must come after Name options)

static cl::opt<std::string> InputURI(cl::Positional, cl::desc("<input URI>"),
                                     cl::init("-"), cl::value_desc("uri"),
                                     cl::cat(MemoDBCategory),
                                     cl::sub(PutCommand), cl::sub(SetCommand));

static llvm::Optional<CID> ReadRef(Store &Db, llvm::StringRef URI) {
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
    Name Name = GetNameFromURI(URI);
    return Db.resolveOptional(Name);
  }
  Node Value;
  switch (format_option) {
  case Format_CBOR:
    Value = Node::load_cbor(
        {reinterpret_cast<const std::uint8_t *>(Buffer->getBufferStart()),
         Buffer->getBufferSize()});
    break;
  case Format_Raw:
    Value = Node(byte_string_arg, Buffer->getBuffer());
    break;
  case Format_JSON:
    Value = Err(Node::loadFromJSON(Buffer->getBuffer()));
    break;
  }
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

static std::unique_ptr<ToolOutputFile> GetOutputFile(bool binary = true) {
  ExitOnError Err("value write: ");
  std::error_code EC;
  auto OutputFile =
      std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::F_None);
  if (EC)
    Err(errorCodeToError(EC));
  if (!binary || Force || !CheckBitcodeOutputToConsole(OutputFile->os()))
    return OutputFile;
  return nullptr;
}

static void WriteValue(const Node &Value) {
  bool binary = format_option != Format_JSON;
  auto OutputFile = GetOutputFile(binary);
  if (OutputFile) {
    switch (format_option) {
    case Format_CBOR: {
      std::vector<std::uint8_t> Buffer;
      Value.save_cbor(Buffer);
      OutputFile->os().write(reinterpret_cast<const char *>(Buffer.data()),
                             Buffer.size());
      break;
    }
    case Format_Raw: {
      if (!Value.is<BytesRef>())
        report_fatal_error("This value cannot be printed in \"raw\" format.");
      auto bytes = Value.as<BytesRef>();
      OutputFile->os().write(reinterpret_cast<const char *>(bytes.data()),
                             bytes.size());
      break;
    }
    case Format_JSON:
      OutputFile->os() << Value << "\n";
      break;
    }
    OutputFile->keep();
  }
}

// memodb export

static cl::list<std::string> NamesToExport(cl::Positional, cl::ZeroOrMore,
                                           cl::desc("<names to export>"),
                                           cl::value_desc("names"),
                                           cl::cat(MemoDBCategory),
                                           cl::sub(ExportCommand));

static int Export() {
  auto OutputFile = GetOutputFile();
  if (!OutputFile)
    return 1;
  std::vector<Name> names;
  for (StringRef NameURI : NamesToExport)
    names.emplace_back(GetNameFromURI(NameURI));
  auto store = Store::open(GetStoreUri());
  CID RootRef = exportToCARFile(OutputFile->os(), *store);
  OutputFile->keep();
  llvm::errs() << "Exported with Root CID: " << RootRef << "\n";
  return 0;
}

// memodb get

static int Get() {
  auto Name = GetNameFromURI(SourceURI);
  auto Db = Store::open(GetStoreUri());
  auto CID = Db->resolveOptional(Name);
  auto Value = CID ? Db->getOptional(*CID) : None;
  if (Value)
    WriteValue(*Value);
  else
    llvm::errs() << "not found\n";
  return 0;
}

// memodb init

static int Init() {
  Store::open(GetStoreUri(), /*create_if_missing*/ true);
  return 0;
}

// memodb invalidate

static int Invalidate() {
  auto db = Store::open(GetStoreUri());
  db->call_invalidate(FuncName);
  return 0;
}

// memodb list-calls

static int ListCalls() {
  auto Db = Store::open(GetStoreUri());
  for (const Call &call : Db->list_calls(FuncName)) {
    outs() << "call:" << call.Name;
    for (const auto &Arg : call.Args)
      outs() << "/" << Arg;
    outs() << "\n";
  }
  return 0;
}

// memodb list-funcs

static int ListFuncs() {
  auto Db = Store::open(GetStoreUri());
  for (llvm::StringRef Func : Db->list_funcs())
    outs() << Func << "\n";
  return 0;
}

// memodb list-heads

static int ListHeads() {
  auto Db = Store::open(GetStoreUri());
  for (const Head &head : Db->list_heads())
    outs() << "head:" << head.Name << "\n";
  return 0;
}

// memodb paths-to

static int PathsTo() {
  auto db = Store::open(GetStoreUri());
  auto ref = ReadRef(*db, TargetURI);
  if (!ref) {
    errs() << "not found\n";
    return 1;
  }
  for (const auto &path : db->list_paths_to(*ref)) {
    outs() << path.first;
    for (const auto &item : path.second)
      outs() << '[' << item << ']';
    outs() << '\n';
  }
  return 0;
}

// memodb put

static int Put() {
  auto Db = Store::open(GetStoreUri());
  auto Ref = ReadRef(*Db, InputURI);
  if (!Ref) {
    errs() << "not found\n";
    return 1;
  }
  outs() << "id:" << *Ref << "\n";
  return 0;
}

// memodb refs-to

static int RefsTo() {
  auto Db = Store::open(GetStoreUri());
  auto Ref = ReadRef(*Db, TargetURI);
  if (!Ref) {
    errs() << "not found\n";
    return 1;
  }
  for (const Name &Name : Db->list_names_using(*Ref)) {
    if (auto head = std::get_if<Head>(&Name)) {
      outs() << "head:" << head->Name << "\n";
    } else if (auto ParentRef = std::get_if<CID>(&Name)) {
      outs() << "id:" << *ParentRef << "\n";
    } else if (auto call = std::get_if<Call>(&Name)) {
      outs() << "call:" << call->Name;
      for (const auto &Arg : call->Args)
        outs() << "/" << Arg;
      outs() << "\n";
    } else {
      llvm_unreachable("impossible value for Name");
    }
  }
  return 0;
}

// memodb set

static int Set() {
  auto Db = Store::open(GetStoreUri());
  auto Name = GetNameFromURI(TargetURI);
  auto Value = ReadRef(*Db, InputURI);
  if (!Value) {
    errs() << "not found\n";
    return 1;
  }
  Db->set(Name, *Value);
  return 0;
}

// memodb transfer

static cl::opt<std::string>
    TargetStoreURI("target-store", cl::Required,
                   cl::desc("URI of the target MemoDB store"),
                   cl::cat(MemoDBCategory), cl::sub(TransferCommand));

static cl::list<std::string> NamesToTransfer(cl::Positional, cl::ZeroOrMore,
                                             cl::desc("<names to transfer>"),
                                             cl::value_desc("names"),
                                             cl::cat(MemoDBCategory),
                                             cl::sub(TransferCommand));

static int Transfer() {
  auto SourceDb = Store::open(GetStoreUri());
  auto TargetDb = Store::open(TargetStoreURI);

  std::function<void(const CID &)> transferRef = [&](const CID &Ref) {
    if (!TargetDb->has(Ref)) {
      Node Value = SourceDb->get(Ref);
      TargetDb->put(Value);
      Value.eachLink(transferRef);
    }
  };

  auto transferName = [&](const Name &Name) {
    errs() << "transferring " << Name << "\n";
    if (const CID *Ref = std::get_if<CID>(&Name)) {
      transferRef(*Ref);
    } else if (const Head *head = std::get_if<Head>(&Name)) {
      auto Result = SourceDb->resolve(Name);
      transferRef(Result);
      TargetDb->set(*head, Result);
    } else if (const Call *call = std::get_if<Call>(&Name)) {
      for (const CID &Arg : call->Args)
        transferRef(Arg);
      CID Result = SourceDb->resolve(Name);
      transferRef(Result);
      TargetDb->set(*call, Result);
    } else {
      llvm_unreachable("impossible Name type");
    }
  };

  if (NamesToTransfer.empty()) {
    for (const Head &head : SourceDb->list_heads())
      transferName(head);
    for (StringRef Func : SourceDb->list_funcs())
      for (const Call &call : SourceDb->list_calls(Func))
        transferName(call);
  } else {
    for (StringRef NameURI : NamesToTransfer)
      transferName(GetNameFromURI(NameURI));
  }
  return 0;
}

// main

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  // Hide LLVM's options, since they're mostly irrelevant.
  ReorganizeOptions([](cl::Option *O) {
    if (!OptionHasCategory(*O, MemoDBCategory)) {
      O->setHiddenFlag(cl::Hidden);
      O->addSubCommand(*cl::AllSubCommands);
    }
  });

  cl::ParseCommandLineOptions(argc, argv, "MemoDB Tools");

  if (ExportCommand) {
    return Export();
  } else if (GetCommand) {
    return Get();
  } else if (InitCommand) {
    return Init();
  } else if (InvalidateCommand) {
    return Invalidate();
  } else if (ListCallsCommand) {
    return ListCalls();
  } else if (ListFuncsCommand) {
    return ListFuncs();
  } else if (ListHeadsCommand) {
    return ListHeads();
  } else if (PathsToCommand) {
    return PathsTo();
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
