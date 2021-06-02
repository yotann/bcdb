#include <cstdlib>
#include <duktape.h>
#include <iostream>
#include <linenoise.h>
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
static cl::SubCommand JSCommand("js", "Interactive Javascript REPL");
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
    return Db.resolve(Name);
  }
  Node Value = Node::load_cbor(
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

static void WriteValue(const Node &Value) {
  auto OutputFile = GetOutputFile();
  if (OutputFile) {
    std::vector<std::uint8_t> Buffer;
    Value.save_cbor(Buffer);
    OutputFile->os().write(reinterpret_cast<const char *>(Buffer.data()),
                           Buffer.size());
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
  std::function<CID(const Node &)> exportValue;

  exportRef = [&](const CID &Ref) {
    if (AlreadyWritten.insert(Ref).second)
      exportValue(Db->get(Ref));
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
  auto addName = [&](const memodb_name &Name) {
    errs() << "exporting " << Name << "\n";
    if (const CID *Ref = std::get_if<CID>(&Name)) {
      exportRef(*Ref);
      IDs.emplace_back(*Ref);
    } else if (const memodb_head *Head = std::get_if<memodb_head>(&Name)) {
      Heads[Head->Name] = Db->resolve(Name);
      exportRef(Heads[Head->Name].as_link());
    } else if (const memodb_call *Call = std::get_if<memodb_call>(&Name)) {
      Node &FuncCalls = Calls[Call->Name];
      if (FuncCalls == Node{})
        FuncCalls = Node::map();
      Node Args = Node(node_list_arg);
      std::string Key;
      for (const CID &Arg : Call->Args) {
        exportRef(Arg);
        Args.emplace_back(Arg);
        Key += std::string(Arg) + "/";
      }
      Key.pop_back();
      auto Result = Db->resolve(Name);
      FuncCalls[Key] = Node::map({{"args", Args}, {"result", Result}});
      exportRef(Result);
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

  Node Header =
      Node::map({{"roots", Node(node_list_arg, {RootRef})}, {"version", 1}});
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
      auto Block =
          Node(llvm::ArrayRef(Padding).take_front(Size)).saveAsIPLD(true);
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
  auto CID = Db->resolveOptional(Name);
  auto Value = CID ? Db->getOptional(*CID) : None;
  if (Value)
    WriteValue(*Value);
  else
    llvm::errs() << "not found\n";
  return 0;
}

// memodb js

namespace {
#include "repl_init.inc"
}

static void jsFatalHandler(void *, const char *Msg) {
  llvm::report_fatal_error(Msg);
}

static duk_ret_t jsPrintAlert(duk_context *Ctx) {
  duk_push_literal(Ctx, " ");
  duk_insert(Ctx, 0);
  duk_join(Ctx, duk_get_top(Ctx) - 1);
  (duk_get_current_magic(Ctx) == 2 ? errs() : outs())
      << duk_require_string(Ctx, -1) << '\n';
  return 0;
}

static duk_context *CompletionCtx = nullptr;

static duk_ret_t jsAddCompletion(duk_context *Ctx) {
  const char *Str = duk_require_string(Ctx, 0);
  auto Arg =
      reinterpret_cast<linenoiseCompletions *>(duk_require_pointer(Ctx, 1));
  linenoiseAddCompletion(Arg, Str);
  return 0;
}

static void jsCompletion(const char *Input, linenoiseCompletions *Arg) {
  if (!CompletionCtx)
    return;
  duk_push_global_stash(CompletionCtx);
  duk_get_prop_string(CompletionCtx, -1, "linenoiseCompletion");
  duk_push_string(CompletionCtx, Input ? Input : "");
  duk_push_c_lightfunc(CompletionCtx, jsAddCompletion, 2, 2, 0);
  duk_push_pointer(CompletionCtx, (void *)Arg);
  duk_call(CompletionCtx, 3);
  duk_pop(CompletionCtx);
}

static char *jsHints(const char *Input, int *Color, int *Bold) {
  if (!CompletionCtx)
    return nullptr;
  duk_push_global_stash(CompletionCtx);
  duk_get_prop_string(CompletionCtx, -1, "linenoiseHints");
  duk_push_string(CompletionCtx, Input ? Input : "");
  duk_call(CompletionCtx, 1);
  if (!duk_is_object(CompletionCtx, -1)) {
    duk_pop_2(CompletionCtx);
    return nullptr;
  }
  duk_get_prop_string(CompletionCtx, -1, "hints");
  const char *Tmp = duk_get_string(CompletionCtx, -1);
  char *Result = Tmp ? strdup(Tmp) : nullptr;
  duk_pop(CompletionCtx);
  duk_pop_2(CompletionCtx);
  return Result;
}

static void jsFreeHints(void *Hints) { free(Hints); }

static int JS() {
  duk_context *Ctx =
      duk_create_heap(nullptr, nullptr, nullptr, nullptr, jsFatalHandler);
  if (!Ctx)
    llvm::report_fatal_error("Couldn't create Duktape heap");

  duk_push_c_lightfunc(Ctx, jsPrintAlert, DUK_VARARGS, 1, 1);
  duk_put_global_literal(Ctx, "print");
  duk_push_c_lightfunc(Ctx, jsPrintAlert, DUK_VARARGS, 1, 2);
  duk_put_global_literal(Ctx, "alert");

  duk_eval_lstring(Ctx, reinterpret_cast<const char *>(repl_init_js),
                   repl_init_js_len);
  duk_push_global_stash(Ctx);
  duk_call(Ctx, 1);

  CompletionCtx = Ctx;
  linenoiseSetCompletionCallback(jsCompletion);
  linenoiseSetFreeHintsCallback(jsFreeHints);
  linenoiseSetHintsCallback(jsHints);
  linenoiseSetMultiLine(1);
  linenoiseHistorySetMaxLen(1000);

  while (const char *line = linenoise("> ")) {
    linenoiseHistoryAdd(line);
    int RC = duk_peval_string(Ctx, line);
    if (RC) {
      errs() << duk_safe_to_stacktrace(Ctx, -1) << '\n';
      duk_pop(Ctx);
    } else {
      duk_push_global_stash(Ctx);
      duk_get_prop_literal(Ctx, -1, "dukFormat");
      duk_dup(Ctx, -3);
      duk_call(Ctx, 1);
      outs() << "= " << duk_to_string(Ctx, -1) << '\n';
      duk_pop_3(Ctx);
    }
    outs().flush();
    errs().flush();
  }
  duk_destroy_heap(Ctx);
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
      Node Value = SourceDb->get(Ref);
      TargetDb->put(Value);
      Value.eachLink(transferRef);
    }
  };

  auto transferName = [&](const memodb_name &Name) {
    errs() << "transferring " << Name << "\n";
    if (const CID *Ref = std::get_if<CID>(&Name)) {
      transferRef(*Ref);
    } else if (const memodb_head *Head = std::get_if<memodb_head>(&Name)) {
      auto Result = SourceDb->resolve(Name);
      transferRef(Result);
      TargetDb->set(*Head, Result);
    } else if (const memodb_call *Call = std::get_if<memodb_call>(&Name)) {
      for (const CID &Arg : Call->Args)
        transferRef(Arg);
      CID Result = SourceDb->resolve(Name);
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
  } else if (JSCommand) {
    return JS();
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
