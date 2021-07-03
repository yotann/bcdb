#include <cstdlib>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <optional>

#include "memodb/NNG.h"
#include "memodb/Server.h"
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"

using namespace memodb;
namespace cl = llvm::cl;

static cl::OptionCategory server_category("MemoDB Server options");

static cl::opt<std::string> listen_url(cl::Positional, cl::Required,
                                       cl::desc("<server address>"),
                                       cl::value_desc("url"),
                                       cl::cat(server_category));

static cl::opt<std::string>
    StoreUriOrEmpty("store", cl::Optional, cl::desc("URI of the MemoDB store"),
                    cl::init(std::string(llvm::StringRef::withNullAsEmpty(
                        std::getenv("MEMODB_STORE")))),
                    cl::cat(server_category));

static llvm::StringRef GetStoreUri() {
  if (StoreUriOrEmpty.empty()) {
    llvm::report_fatal_error(
        "You must provide a MemoDB store URI, such as "
        "sqlite:/tmp/example.bcdb, using the -store option or "
        "the MEMODB_STORE environment variable.");
  }
  return StoreUriOrEmpty;
}

namespace {
struct NNGRequest : public Request {
  nng::HTTPRequestView req;

  NNGRequest(nng::HTTPRequestView req) : req(req) {}
  ~NNGRequest() override {}

  llvm::StringRef getMethod() const override { return req.getMethod(); }

  llvm::StringRef getURI() const override { return req.getURI(); }

  std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const override {
    return req.getHeader(key);
  }
};
} // end anonymous namespace

namespace {
struct NNGResponse : public Response {
  nng::HTTPResponse res;

  NNGResponse(const Request &request) : Response(request) {
    llvm::ExitOnError Err("memodb-server NNGResponse: ");
    res = Err(nng::HTTPResponse::alloc());
  }

  ~NNGResponse() override {}

  void sendStatusImpl(std::uint16_t status) override {
    llvm::ExitOnError Err("memodb-server sendStatus: ");
    Err(res.setStatus(status));
  }

  void sendHeaderImpl(const llvm::Twine &key,
                      const llvm::Twine &value) override {
    llvm::ExitOnError Err("memodb-server sendHeader: ");
    Err(res.addHeader(key, value));
  }

  void sendBodyImpl(const llvm::Twine &body) override {
    llvm::ExitOnError Err("memodb-server sendBody: ");
    Err(res.copyData(body));
  }
};
} // end anonymous namespace

static Server *g_server = nullptr;

static void httpHandler(nng_aio *raw_aio) {
  llvm::ExitOnError Err("memodb-server HTTP handler: ");
  nng::AIOView aio(raw_aio);

  nng::HTTPRequestView nng_req(
      reinterpret_cast<nng_http_req *>(aio.getInput(0)));
  NNGRequest req(nng_req);
  NNGResponse res(req);

  g_server->handleRequest(req, res);

  aio.setOutput(0, res.res.release());
  aio.finish(0);
}

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  cl::HideUnrelatedOptions(server_category);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Server");

  llvm::ExitOnError Err("memodb-server: ");
  auto store = Store::open(GetStoreUri());
  Server server(*store);
  g_server = &server;
  auto url = Err(nng::URL::parse(listen_url));
  auto http_server = Err(nng::HTTPServer::hold(url));

  auto handler = Err(nng::HTTPHandler::alloc("/", httpHandler));
  Err(handler.setMethod(std::nullopt));
  Err(handler.setTree());
  Err(http_server.addHandler(std::move(handler)));

  Err(http_server.start());

  while (true) {
    nng::msleep(1000);
  }

  return 0;
}
