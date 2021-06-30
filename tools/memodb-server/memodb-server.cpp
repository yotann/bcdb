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

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  cl::HideUnrelatedOptions(server_category);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Server");

  llvm::ExitOnError Err("memodb-server: ");
  auto url = Err(nng::URL::parse(listen_url));
  auto server = Err(nng::HTTPServer::hold(url));
  auto handler = Err(nng::HTTPHandler::allocStatic(
      "/",
      "<?DOCTYPE html>\n<title>MemoDB server</title>\n<h1>MemoDB server</h1>\n",
      "text/html"));
  Err(server.addHandler(std::move(handler)));
  Err(server.start());

  while (true) {
    nng::msleep(1000);
  }

  return 0;
}
