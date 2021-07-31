#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <optional>

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/util/platform.h>

#include "memodb/Evaluator.h"
#include "memodb/HTTP.h"
#include "memodb/Server.h"
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"
#include "memodb/URI.h"

using namespace memodb;
namespace cl = llvm::cl;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;

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

static void checkErr(int err) {
  if (err)
    llvm::report_fatal_error(nng_strerror(err));
}

static std::mutex stdout_mutex;

static Server *g_server = nullptr;

namespace {

struct AioDeleter {
  void operator()(nng_aio *aio) { nng_aio_free(aio); }
};

struct HTTPHandlerDeleter {
  void operator()(nng_http_handler *handler) { nng_http_handler_free(handler); }
};

struct HTTPResponseDeleter {
  void operator()(nng_http_res *res) { nng_http_res_free(res); }
};

struct HTTPServerDeleter {
  void operator()(nng_http_server *server) { nng_http_server_release(server); }
};

struct URLDeleter {
  void operator()(nng_url *url) { nng_url_free(url); }
};

}; // end anonymous namespace

std::unique_ptr<nng_url, URLDeleter> parse_url(const Twine &str) {
  SmallVector<char, 0> buffer;
  nng_url *result;
  checkErr(
      nng_url_parse(&result, str.toNullTerminatedStringRef(buffer).data()));
  return std::unique_ptr<nng_url, URLDeleter>(result);
}

std::unique_ptr<nng_http_server, HTTPServerDeleter>
http_server_hold(nng_url *url) {
  nng_http_server *result;
  checkErr(nng_http_server_hold(&result, url));
  return std::unique_ptr<nng_http_server, HTTPServerDeleter>(result);
}

std::unique_ptr<nng_http_handler, HTTPHandlerDeleter>
http_handler_alloc(const Twine &path, void (*func)(nng_aio *)) {
  nng_http_handler *result;
  SmallVector<char, 0> buffer;
  auto path_str = path.toNullTerminatedStringRef(buffer);
  checkErr(nng_http_handler_alloc(&result, path_str.data(), func));
  return std::unique_ptr<nng_http_handler, HTTPHandlerDeleter>(result);
}

void cancelHandler(nng_aio *http_aio, void *arg, int err) {
  // Just cancel the timeout, and handle everything in the timeout handler.
  nng_aio_cancel(static_cast<nng_aio *>(arg));
}

namespace {
struct NNGRequest : public HTTPRequest,
                    public std::enable_shared_from_this<NNGRequest> {
  nng_http_req *req;
  std::unique_ptr<nng_http_res, HTTPResponseDeleter> res;
  nng_aio *http_aio;
  std::unique_ptr<nng_aio, AioDeleter> wait_aio;
  std::shared_ptr<NNGRequest> self_if_deferred;

  NNGRequest(nng_http_req *req, nng_aio *http_aio)
      : req(req), http_aio(http_aio) {
    nng_http_res *res_tmp;
    checkErr(nng_http_res_alloc(&res_tmp));
    res.reset(res_tmp);
  }

  ~NNGRequest() override {}

  llvm::StringRef getMethodString() const override {
    return nng_http_req_get_method(req);
  }

  std::optional<URI> getURI() const override {
    return URI::parse(nng_http_req_get_uri(req));
  }

  std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const override {
    SmallVector<char, 0> buffer;
    auto key_str = key.toNullTerminatedStringRef(buffer);
    auto result = nng_http_req_get_header(req, key_str.data());
    if (!result)
      return std::nullopt;
    return result;
  }

  llvm::StringRef getBody() const override {
    void *body;
    size_t size;
    nng_http_req_get_data(req, &body, &size);
    return StringRef(reinterpret_cast<const char *>(body), size);
  }

  void sendStatus(std::uint16_t status) override {
    checkErr(nng_http_res_set_status(res.get(), status));
  }

  void sendHeader(llvm::StringRef key, const llvm::Twine &value) override {
    SmallVector<char, 64> key_buffer, value_buffer;
    auto key_str = Twine(key).toNullTerminatedStringRef(key_buffer);
    auto value_str = value.toNullTerminatedStringRef(value_buffer);
    checkErr(
        nng_http_res_add_header(res.get(), key_str.data(), value_str.data()));
  }

  void sendBody(const llvm::Twine &body) override {
    SmallVector<char, 0> buffer;
    auto body_str = body.toStringRef(buffer);
    checkErr(
        nng_http_res_copy_data(res.get(), body_str.data(), body_str.size()));
    writeLog(body_str.size());
    actuallySend();
  }

  void sendEmptyBody() override {
    sendHeader("Content-Length", "0");
    writeLog(0);
    actuallySend();
  }

  void actuallySend() {
    assert(state != State::Cancelled && state != State::Done);
    nng_aio_set_output(http_aio, 0, res.release());
    nng_aio_finish(http_aio, 0);
    state = State::Done;
    if (self_if_deferred) {
      // The self_if_deferred variable is preventing this NNGRequest from being
      // deleted. We can't delete it until wait_aio is successfully cancelled
      // and waitHandler() is called.
      nng_aio_cancel(wait_aio.get());
    }
  }

  void writeLog(size_t body_size) {
    // https://en.wikipedia.org/wiki/Common_Log_Format

    StringRef ip_address = "-"; // TODO: NNG doesn't seem to expose this.

    // TODO: ensure the locale is set correctly.
    char time_buffer[32] = "";
    std::time_t time = std::time(nullptr);
    std::strftime(time_buffer, sizeof(time_buffer), "%d/%b/%Y:%H:%M:%S %z",
                  std::localtime(&time));

    SmallVector<char, 256> buffer;
    auto line =
        (ip_address + " - - [" + time_buffer + "] \"" + getMethodString() +
         " " + nng_http_req_get_uri(req) + " " + nng_http_req_get_version(req) +
         "\" " + Twine(nng_http_res_get_status(res.get())) + " " +
         (body_size ? Twine(body_size) : Twine("-")))
            .toStringRef(buffer);

    std::lock_guard lock(stdout_mutex);
    llvm::outs() << line << "\n";
  }

  void waitHandler() {
    // XXX: self must be declared before lock is, to ensure the lock is
    // released before self is (potentially) deleted.
    std::shared_ptr<NNGRequest> self;

    std::lock_guard lock(mutex);

    // Make sure this NNGRequest exists until this function returns, at which
    // point it may be deleted (depending on whether or not the Server has a
    // shared_ptr<NNGRequest> pointing to it).
    self = std::move(self_if_deferred);

    // We can't free wait_aio in this function because NNG would deadlock.

    if (state == State::Done) {
      // Nothing to do. The Server must have responded to this request while we
      // were still waiting to lock the mutex.
      return;
    }

    assert(state == State::Waiting);
    int result = nng_aio_result(wait_aio.get());
    state = result == 0 ? State::TimedOut : State::Cancelled;
    g_server->handleRequest(self);
    assert(state == (result == 0 ? State::Done : State::Cancelled));
  }

  static void waitHandlerStatic(void *self) {
    static_cast<NNGRequest *>(self)->waitHandler();
  }

  void deferWithTimeout(unsigned seconds) override {
    assert(state == State::New);
    state = State::Waiting;

    self_if_deferred = shared_from_this();

    nng_aio *tmp_aio;
    checkErr(nng_aio_alloc(&tmp_aio, waitHandlerStatic, this));
    wait_aio.reset(tmp_aio);
    nng_sleep_aio(seconds * 1000, wait_aio.get());

    nng_aio_defer(http_aio, cancelHandler, wait_aio.get());
  }
};
} // end anonymous namespace

static void httpHandler(nng_aio *aio) {
  auto nng_req = reinterpret_cast<nng_http_req *>(nng_aio_get_input(aio, 0));
  auto req = std::make_shared<NNGRequest>(nng_req, aio);
  std::lock_guard lock(req->mutex);
  g_server->handleRequest(req);
}

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  cl::HideUnrelatedOptions(server_category);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Server");

  llvm::ExitOnError Err("memodb-server: ");
  auto evaluator = Evaluator(Store::open(GetStoreUri()));
  Server server(evaluator);
  g_server = &server;
  auto url = parse_url(listen_url);
  auto http_server = http_server_hold(url.get());

  auto handler = http_handler_alloc("/", httpHandler);
  checkErr(nng_http_handler_set_method(handler.get(), nullptr));
  checkErr(nng_http_handler_set_tree(handler.get()));
  checkErr(nng_http_server_add_handler(http_server.get(), handler.release()));

  checkErr(nng_http_server_start(http_server.get()));

  llvm::errs() << "Server started!\n";

  while (true)
    nng_msleep(1000);

  return 0;
}
