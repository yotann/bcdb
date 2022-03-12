#include <cstdlib>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>

#include <boost/asio.hpp>
#include <boost/assert/source_location.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/throw_exception.hpp>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>

#include "memodb/HTTP.h"
#include "memodb/Request.h"
#include "memodb/Server.h"
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"
#include "memodb/URI.h"

using namespace memodb;
namespace cl = llvm::cl;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
namespace local = boost::asio::local;

static cl::OptionCategory server_category("MemoDB Server options");

static cl::opt<std::string> listen_url(cl::Positional, cl::Required,
                                       cl::desc("<server address>"),
                                       cl::value_desc("url"),
                                       cl::cat(server_category));

static cl::opt<std::string> StoreUriOrEmpty(
    "store", cl::Optional, cl::desc("URI of the MemoDB store"),
    cl::init(std::string(llvm::StringRef(std::getenv("MEMODB_STORE")))),
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

static std::mutex g_stdout_mutex;

static Server *g_server = nullptr;

namespace {
class ConnectionBase;
class http_request : public HTTPRequest {
public:
  http_request(std::shared_ptr<ConnectionBase> connection,
               http::request<http::string_body> &request,
               http::response<http::string_body> &response)
      : HTTPRequest(request.method_string(), URI::parse(request.target())),
        connection(std::move(connection)), request(request),
        response(response) {
    response.version(request.version());
  }

  ~http_request() override {}

  std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const override {
    SmallVector<char, 64> key_buffer;
    auto iter = request.find(key.toStringRef(key_buffer));
    if (iter == request.end())
      return std::nullopt;
    return iter->value();
  }

  llvm::StringRef getBody() const override { return request.body(); }

  void sendStatus(std::uint16_t status) override { response.result(status); }

  void sendHeader(llvm::StringRef key, const llvm::Twine &value) override {
    SmallVector<char, 64> value_buffer;
    response.insert(key, value.toStringRef(value_buffer));
  }

  void sendBody(const llvm::Twine &body) override;
  void sendEmptyBody() override;

private:
  std::shared_ptr<ConnectionBase> connection;
  http::request<http::string_body> &request;
  http::response<http::string_body> &response;
};
} // end anonymous namespace

namespace {
class ConnectionBase : public std::enable_shared_from_this<ConnectionBase> {
public:
  ConnectionBase() {}
  virtual ~ConnectionBase() {}

  void start() { readRequest(); }

protected:
  friend class http_request;
  template <typename Protocol> friend class Connection;
  friend class local_connection;
  beast::flat_buffer buffer{8192};
  http::request<http::string_body> request;
  http::response<http::string_body> response;
  std::optional<http_request> wrapped_request;

  virtual void readRequest() = 0;
  virtual void writeRemoteAddress(std::ostream &os) const = 0;
  virtual void writeResponse() = 0;

  void processRequest() {
    wrapped_request.emplace(shared_from_this(), request, response);
    g_server->handleRequest(*wrapped_request);
  }

  void writeLog() {
    // https://en.wikipedia.org/wiki/Common_Log_Format

    // There are so many successful requests, writing the log is actually a
    // bottleneck. So let's only log failures.
    if (response.result_int() >= 200 && response.result_int() <= 299)
      return;

    auto body_size = response.body().size();

    // TODO: ensure the locale is set correctly.
    char time_buffer[32] = "";
    std::time_t time = std::time(nullptr);
    std::strftime(time_buffer, sizeof(time_buffer), "%d/%b/%Y:%H:%M:%S %z",
                  std::localtime(&time));

    std::lock_guard lock(g_stdout_mutex);
    writeRemoteAddress(std::cout);
    std::cout << " - - [" << time_buffer << "] \"" << request.method_string()
              << " " << request.target() << " HTTP/" << (request.version() / 10)
              << "." << (request.version() % 10) << "\" "
              << response.result_int() << " " << body_size << "\r\n";
  }

  void sendResponse() {
    wrapped_request->responded = true;
    wrapped_request.reset();
    response.content_length(response.body().size());
    writeLog();
    request = {};
    writeResponse();
  }
};
} // end anonymous namespace

template <typename T>
static void writeEndpoint(std::ostream &os, const T &endpoint) {
  os << "-";
}

static void writeEndpoint(std::ostream &os, const tcp::endpoint &endpoint) {
  os << endpoint.address();
}

void http_request::sendBody(const llvm::Twine &body) {
  response.body() = body.str();
  connection->sendResponse();
}

void http_request::sendEmptyBody() {
  response.body().clear();
  connection->sendResponse();
}

namespace {
template <typename Protocol> class Connection : public ConnectionBase {
public:
  Connection(typename Protocol::socket socket) : socket(std::move(socket)) {}
  ~Connection() override {}

protected:
  void readRequest() override {
    auto self = shared_from_this();
    http::async_read(socket, buffer, request,
                     [self](beast::error_code ec, std::size_t) {
                       if (!ec)
                         self->processRequest();
                     });
  }

  void writeRemoteAddress(std::ostream &os) const override {
    writeEndpoint(os, socket.remote_endpoint());
  }

  void writeResponse() override {
    auto self = shared_from_this();
    http::async_write(socket, response,
                      [self](beast::error_code ec, std::size_t) {
                        self->response = {};
                        if (!ec)
                          self->readRequest();
                      });
  }

private:
  typename Protocol::socket socket;
};
} // end anonymous namespace

template <typename Protocol>
static void asyncAccept(typename Protocol::acceptor &acceptor) {
  acceptor.async_accept(
      [&](beast::error_code ec, typename Protocol::socket socket) {
        if (!ec)
          std::make_shared<Connection<Protocol>>(std::move(socket))->start();
        asyncAccept<Protocol>(acceptor);
      });
}

template <typename Protocol>
static void serve(const typename Protocol::endpoint &local_endpoint) {
  net::io_context ioc{1};
  typename Protocol::acceptor acceptor{ioc, local_endpoint};
  asyncAccept<Protocol>(acceptor);
  llvm::errs() << "Server started!\n";
  ioc.run();
}

int main(int argc, char **argv) {
  InitTool X(argc, argv);
  cl::HideUnrelatedOptions(server_category);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Server");

  // We open the store *before* printing anything or opening a socket. Opening
  // the store can take a long time if database logs need to be replayed.
  auto store = Store::open(GetStoreUri());

  Server server(*store);
  g_server = &server;

  auto uri_or_none = URI::parse(listen_url);
  if (!uri_or_none) {
    llvm::errs() << "Invalid URL: " << listen_url << "\n";
    llvm::errs() << "Try http://127.0.0.1:8000/\n";
    return 1;
  }
  net::io_context ioc{1};
  if (uri_or_none->scheme == "http" || uri_or_none->scheme == "tcp") {
    auto const address = net::ip::make_address(uri_or_none->host);
    unsigned short port = static_cast<unsigned short>(uri_or_none->port);
    serve<tcp>({address, port});
  } else if (uri_or_none->scheme == "unix") {
    serve<local::stream_protocol>(uri_or_none->getPathString());
  } else {
    llvm::errs() << "Invalid scheme: " << uri_or_none->scheme << "\n";
    llvm::errs() << "Use http, tcp, or unix\n";
    return 1;
  }

  return 0;
}
