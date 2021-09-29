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
#include "memodb/Evaluator.h"
#include "memodb/Server.h"
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"
#include "memodb/URI.h"

using namespace llvm;
using namespace memodb;

cl::OptionCategory MemoDBCategory("MemoDB options");

static cl::SubCommand AddCommand("add", "Add a value to the store");
static cl::SubCommand DeleteCommand("delete",
                                    "Delete a value, or invalidate calls");
static cl::SubCommand EvaluateCommand(
    "evaluate",
    "Evaluate an arbitrary func (if the func is built in to memodb)");
static cl::SubCommand ExportCommand("export", "Export values to a CAR file");
static cl::SubCommand GetCommand("get", "Get a value");
static cl::SubCommand InitCommand("init", "Initialize a store");
static cl::SubCommand
    PathsToCommand("paths-to",
                   "Find paths from a head or call that reach a value");
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
  Format_Auto,
  Format_CBOR,
  Format_Raw,
  Format_JSON,
};

static cl::opt<Format> format_option(
    "format", cl::Optional, cl::desc("Format for input and output nodes"),
    cl::values(clEnumValN(Format_Auto, "auto", "MemoDB JSON or URI."),
               clEnumValN(Format_CBOR, "cbor", "original CBOR."),
               clEnumValN(Format_Raw, "raw",
                          "raw binary data without CBOR wrapper."),
               clEnumValN(Format_JSON, "json", "MemoDB JSON.")),
    cl::init(Format_Auto), cl::sub(AddCommand), cl::sub(GetCommand),
    cl::sub(SetCommand));

// Name options

static cl::opt<std::string> SourceURI(cl::Positional, cl::Required,
                                      cl::desc("<source URI>"),
                                      cl::value_desc("uri"),
                                      cl::cat(MemoDBCategory),
                                      cl::sub(GetCommand));

static cl::opt<std::string>
    TargetURI(cl::Positional, cl::Required, cl::desc("<target URI>"),
              cl::value_desc("uri"), cl::cat(MemoDBCategory),
              cl::sub(DeleteCommand), cl::sub(PathsToCommand),
              cl::sub(RefsToCommand), cl::sub(SetCommand));

static cl::list<std::string> FuncNames(cl::Positional, cl::OneOrMore,
                                       cl::desc("<function names>"),
                                       cl::value_desc("funcs"),
                                       cl::cat(MemoDBCategory));

static Name GetNameFromURI(llvm::StringRef URI) {
  auto result = Name::parse(URI);
  if (!result)
    report_fatal_error("invalid name URI");
  return *result;
}

// input options (XXX: must come after Name options)

static cl::opt<std::string> InputURI(cl::Positional, cl::desc("<input URI>"),
                                     cl::init("-"), cl::value_desc("uri"),
                                     cl::cat(MemoDBCategory),
                                     cl::sub(SetCommand));

static llvm::Optional<CID> ReadRef(Store &Db, llvm::StringRef URI) {
  ExitOnError Err("value read: ");
  std::unique_ptr<MemoryBuffer> Buffer;
  if (URI == "-") {
    Buffer = Err(errorOrToExpected(MemoryBuffer::getSTDIN()));
  } else if (llvm::StringRef(URI).startswith("file:")) {
    auto Parsed = ::URI::parse(URI, /*allow_dot_segments*/ true);
    if (!Parsed || !Parsed->host.empty() || Parsed->port != 0 ||
        !Parsed->query_params.empty() || !Parsed->fragment.empty())
      report_fatal_error("invalid input URI");
    Buffer =
        Err(errorOrToExpected(MemoryBuffer::getFile(Parsed->getPathString())));
  } else {
    Name Name = GetNameFromURI(URI);
    return Db.resolveOptional(Name);
  }
  Node Value;
  switch (format_option) {
  case Format_CBOR: {
    Value = llvm::cantFail(Node::loadFromCBOR(
        Db, {reinterpret_cast<const std::uint8_t *>(Buffer->getBufferStart()),
             Buffer->getBufferSize()}));
    break;
  }
  case Format_Raw:
    Value = Node(byte_string_arg, Buffer->getBuffer());
    break;
  case Format_Auto:
  case Format_JSON:
    Value = Err(Node::loadFromJSON(Db, Buffer->getBuffer()));
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

// Request subclass

namespace {
class CLIRequest : public Request {
public:
  CLIRequest(Method method, const URI &uri) : method(method), uri(uri) {}
  std::optional<Method> getMethod() const override { return method; }
  std::optional<URI> getURI() const override { return uri; }

  std::optional<Node>
  getContentNode(Store &store,
                 const std::optional<Node> &default_node) override {
    return std::nullopt;
  }

  ContentType chooseNodeContentType(const Node &node) override {
    switch (format_option) {
    case Format_CBOR:
      return ContentType::CBOR;
    case Format_Raw:
      if (!node.is<BytesRef>())
        report_fatal_error("This value cannot be printed in \"raw\" format.");
      return ContentType::OctetStream;
    case Format_JSON:
      return ContentType::JSON;
    case Format_Auto:
    default:
      return ContentType::Plain;
    }
  }

  bool sendETag(std::uint64_t etag, CacheControl cache_control) override {
    return false;
  }

  void sendContent(ContentType type, const llvm::StringRef &body) override {
    bool binary = type != ContentType::JSON && type != ContentType::Plain;
    auto output_file = GetOutputFile(binary);
    if (output_file)
      output_file->os().write(body.data(), body.size());
    responded = true;
  }

  void sendAccepted() override {
    errs() << "accepted\n";
    responded = true;
  }

  void sendCreated(const std::optional<URI> &path) override {
    if (path && !path->path_segments.empty() && path->path_segments[0] == "cid")
      outs() << path->path_segments[1] << "\n";
    else if (path)
      outs() << path->encode() << "\n";
    else
      errs() << "created\n";
    responded = true;
  }

  void sendDeleted() override {
    outs() << "deleted\n";
    responded = true;
  }

  void sendError(Status status, std::optional<llvm::StringRef> type,
                 llvm::StringRef title,
                 const std::optional<llvm::Twine> &detail) override {
    errs() << "error: " << title << "\n";
    if (detail)
      errs() << *detail << "\n";
    exit(1);
  }

  void sendMethodNotAllowed(llvm::StringRef allow) override {
    errs() << "invalid operation for this URI\n";
    exit(1);
  }

  Method method;
  URI uri;
};
} // end anonymous namespace

// memodb add

static int Add() {
  auto store = Store::open(GetStoreUri());
  auto ref = ReadRef(*store, "-");
  outs() << Name(*ref) << "\n";
  return 0;
}

// memodb delete

static int Delete() {
  auto store = Store::open(GetStoreUri());
  Server server(*store);
  auto uri = URI::parse(TargetURI);
  if (!uri) {
    errs() << "invalid URI\n";
    return 1;
  }
  CLIRequest request(Request::Method::DELETE, *uri);
  server.handleRequest(request);
  return 0;
}

// memodb evaluate

static cl::opt<std::string> CallToEvaluate(cl::Positional, cl::Required,
                                           cl::desc("<call to evaluate>"),
                                           cl::value_desc("call"),
                                           cl::cat(MemoDBCategory),
                                           cl::sub(EvaluateCommand));

static int Evaluate() {
  auto evaluator = Evaluator::create(GetStoreUri(), /*num_threads*/ 1);
  auto name = GetNameFromURI(CallToEvaluate);
  if (!std::holds_alternative<Call>(name))
    report_fatal_error("You must provide a call starting with /call/");
  auto result = evaluator->evaluate(std::get<Call>(name));
  outs() << result.getCID() << "\n";
  return 0;
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
  CID RootRef = exportToCARFile(OutputFile->os(), *store, names);
  OutputFile->keep();
  llvm::errs() << "Exported with Root CID: " << RootRef << "\n";
  return 0;
}

// memodb get

static int Get() {
  auto store = Store::open(GetStoreUri());
  Server server(*store);
  auto uri = URI::parse(SourceURI);
  if (!uri) {
    errs() << "invalid URI\n";
    return 1;
  }
  CLIRequest request(Request::Method::GET, *uri);
  server.handleRequest(request);
  return 0;
}

// memodb init

static int Init() {
  Store::open(GetStoreUri(), /*create_if_missing*/ true);
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

// memodb refs-to

static int RefsTo() {
  auto Db = Store::open(GetStoreUri());
  auto Ref = ReadRef(*Db, TargetURI);
  if (!Ref) {
    errs() << "not found\n";
    return 1;
  }
  for (const Name &Name : Db->list_names_using(*Ref))
    outs() << Name << "\n";
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

  if (AddCommand) {
    return Add();
  } else if (DeleteCommand) {
    return Delete();
  } else if (EvaluateCommand) {
    return Evaluate();
  } else if (ExportCommand) {
    return Export();
  } else if (GetCommand) {
    return Get();
  } else if (InitCommand) {
    return Init();
  } else if (PathsToCommand) {
    return PathsTo();
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
