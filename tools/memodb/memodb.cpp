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
#include <string>

#include "bcdb/LLVMCompat.h"
#include "memodb/memodb.h"

using namespace llvm;

cl::OptionCategory MemoDBCategory("MemoDB options");

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
    return memodb_ref(Parsed.Path);
  else if (Parsed.Scheme == "call") {
    std::vector<memodb_ref> Args;
    if (Parsed.PathSegments.empty())
      report_fatal_error("invalid name URI");
    auto FuncName = Parsed.PathSegments.front();
    for (const auto &Arg : llvm::ArrayRef(Parsed.PathSegments).drop_front())
      Args.emplace_back(Arg);
    return memodb_call(FuncName, Args);
  } else
    report_fatal_error("invalid name URI");
}

// input options (XXX: must come after memodb_name options)

static cl::opt<std::string> InputURI(cl::Positional, cl::desc("<input URI>"),
                                     cl::init("-"), cl::value_desc("uri"),
                                     cl::cat(MemoDBCategory),
                                     cl::sub(PutCommand), cl::sub(SetCommand));

static memodb_ref ReadRef(memodb_db &Db, llvm::StringRef URI) {
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
    if (memodb_ref *Ref = std::get_if<memodb_ref>(&Name))
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
                           cl::sub(GetCommand));

static cl::opt<std::string> OutputFilename("o", cl::desc("<output file>"),
                                           cl::init("-"),
                                           cl::value_desc("filename"),
                                           cl::sub(GetCommand));

static void WriteValue(const memodb_value &Value) {
  ExitOnError Err("value write: ");
  std::error_code EC;
  auto OutputFile =
      std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::F_None);
  if (EC)
    Err(errorCodeToError(EC));
  if (Force || !CheckBitcodeOutputToConsole(OutputFile->os())) {
    std::vector<std::uint8_t> Buffer;
    Value.save_cbor(Buffer);
    OutputFile->os().write(reinterpret_cast<const char *>(Buffer.data()),
                           Buffer.size());
    OutputFile->keep();
  }
}

// memodb get

static int Get() {
  auto Name = GetNameFromURI(SourceURI);
  auto Db = memodb_db_open(GetUri());
  auto Value = Db->getOptional(Name);
  if (Value && !std::holds_alternative<memodb_ref>(Name))
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
  memodb_ref Ref = ReadRef(*Db, InputURI);
  outs() << "id:" << Ref << "\n";
  return 0;
}

// memodb refs-to

static int RefsTo() {
  auto Db = memodb_db_open(GetUri());
  memodb_ref Ref = ReadRef(*Db, TargetURI);
  for (const memodb_name &Name : Db->list_names_using(Ref)) {
    if (auto Head = std::get_if<memodb_head>(&Name))
      outs() << "head:" << Head->Name << "\n";
    else if (auto ParentRef = std::get_if<memodb_ref>(&Name))
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

template <typename T>
memodb_value transformRefs(const memodb_value &Value, T F) {
  if (Value.type() == memodb_value::REF) {
    return F(Value.as_ref());
  } else if (Value.type() == memodb_value::ARRAY) {
    memodb_value::array_t Result;
    for (const memodb_value &Item : Value.array_items())
      Result.push_back(transformRefs(Item, F));
    return memodb_value(Result);
  } else if (Value.type() == memodb_value::MAP) {
    memodb_value::map_t Result;
    for (const auto &Item : Value.map_items())
      Result[transformRefs(Item.first, F)] = transformRefs(Item.second, F);
    return memodb_value(Result);
  } else {
    return Value;
  }
}

static int Transfer() {
  auto SourceDb = memodb_db_open(GetUri());
  auto TargetDb = memodb_db_open(TargetDatabaseURI);

  std::map<memodb_ref, memodb_ref> RefMapping;

  std::function<memodb_ref(const memodb_ref &)> transferRef =
      [&](const memodb_ref &Ref) {
        auto Inserted = RefMapping.insert(std::make_pair(Ref, memodb_ref()));
        if (Inserted.second) {
          memodb_value Value = SourceDb->get(Ref);
          Value = transformRefs(Value, [&](const memodb_ref &SubRef) {
            return transferRef(SubRef);
          });
          Inserted.first->second = TargetDb->put(Value);
        }
        return Inserted.first->second;
      };

  auto transferName = [&](const memodb_name &Name) {
    errs() << "transferring " << Name << "\n";
    if (const memodb_ref *Ref = std::get_if<memodb_ref>(&Name)) {
      memodb_ref Result = transferRef(*Ref);
      outs() << Result << "\n";
    } else if (const memodb_head *Head = std::get_if<memodb_head>(&Name)) {
      memodb_ref Result = transferRef(SourceDb->get(Name).as_ref());
      TargetDb->set(Name, Result);
    } else if (const memodb_call *Call = std::get_if<memodb_call>(&Name)) {
      memodb_call NewCall(Call->Name, {});
      for (const memodb_ref &Arg : Call->Args)
        NewCall.Args.emplace_back(transferRef(Arg));
      memodb_ref Result = transferRef(SourceDb->get(Name).as_ref());
      TargetDb->set(NewCall, Result);
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

  if (GetCommand) {
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
