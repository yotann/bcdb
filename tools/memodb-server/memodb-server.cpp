#include <llvm/Support/CommandLine.h>

#include "memodb/NNG.h"
#include "memodb/ToolSupport.h"

using namespace memodb;
namespace cl = llvm::cl;

static cl::OptionCategory server_category("MemoDB Server options");

static cl::opt<std::string> listen_url(cl::Positional, cl::Required,
                                       cl::desc("<server port>"),
                                       cl::value_desc("url"),
                                       cl::cat(server_category));

static void httpHandler(nng_aio *raw_aio) {
  llvm::ExitOnError Err("memodb-server HTTP handler: ");
  nng::AIOView aio(raw_aio);

  nng::HTTPRequestView req(reinterpret_cast<nng_http_req *>(aio.getInput(0)));

  auto res = Err(nng::HTTPResponse::alloc());
  Err(res.setHeader("secret-header", "zzt"));
  Err(res.copyData("Hello, " + req.getURI() + "!\r\n"));
  aio.setOutput(0, res.release());
  aio.finish(0);
}

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  cl::HideUnrelatedOptions(server_category);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Server");

  llvm::ExitOnError Err("memodb-server: ");
  auto url = Err(nng::URL::parse(listen_url));
  auto server = Err(nng::HTTPServer::hold(url));

  auto handler = Err(nng::HTTPHandler::alloc("/", httpHandler));
  Err(handler.setMethod(std::nullopt));
  Err(handler.setTree());
  Err(server.addHandler(std::move(handler)));

  Err(server.start());

  while (true) {
    nng::msleep(1000);
  }

  return 0;
}
