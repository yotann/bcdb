#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <nngpp/nngpp.h>
#include <nngpp/protocol/rep0.h>
#include <string>
#include <utility>

#include "memodb/memodb.h"

using namespace llvm;

static cl::OptionCategory BrokerCategory("MemoDB Broker options");

static cl::opt<std::string> ListenURL(cl::Positional, cl::Required,
                                      cl::desc("<broker port>"),
                                      cl::value_desc("port"),
                                      cl::cat(BrokerCategory));

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  cl::HideUnrelatedOptions(BrokerCategory);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Broker");

  auto sock = nng::rep::v0::open();
  sock.listen(ListenURL.c_str());

  while (true) {
    auto msg = sock.recv_msg();
    sock.send(std::move(msg));
  }

  return 0;
}
