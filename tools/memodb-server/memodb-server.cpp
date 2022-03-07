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

#if defined(BOOST_NO_EXCEPTIONS)

void boost::throw_exception(std::exception const &e) {
  llvm::report_fatal_error(e.what());
}

void boost::throw_exception(std::exception const &e,
                            boost::source_location const &loc) {
  throw_exception(e);
}

#endif // defined(BOOST_NO_EXCEPTIONS)

static std::mutex stdout_mutex;

static Server *g_server = nullptr;

static StringRef makeStringRef(beast::string_view value) {
  return StringRef(value.data(), value.size());
}

static beast::string_view makeStringView(llvm::StringRef value) {
  return beast::string_view(value.data(), value.size());
}

namespace {
class http_connection;
class http_request : public HTTPRequest {
public:
  http_request(std::shared_ptr<http_connection> connection,
               http::request<http::string_body> &request,
               http::response<http::string_body> &response)
      : HTTPRequest(makeStringRef(request.method_string()),
                    URI::parse(makeStringRef(request.target()))),
        connection(std::move(connection)), request(request),
        response(response) {
    response.version(request.version());
  }

  ~http_request() override {}

  std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const override {
    SmallVector<char, 64> key_buffer;
    auto iter = request.find(makeStringView(key.toStringRef(key_buffer)));
    if (iter == request.end())
      return std::nullopt;
    return makeStringRef(iter->value());
  }

  llvm::StringRef getBody() const override { return request.body(); }

  void sendStatus(std::uint16_t status) override { response.result(status); }

  void sendHeader(llvm::StringRef key, const llvm::Twine &value) override {
    SmallVector<char, 64> value_buffer;
    auto value_str = value.toStringRef(value_buffer);
    response.insert(makeStringView(key), makeStringView(value_str));
  }

  void sendBody(const llvm::Twine &body) override;
  void sendEmptyBody() override;

private:
  std::shared_ptr<http_connection> connection;
  http::request<http::string_body> &request;
  http::response<http::string_body> &response;
};
} // end anonymous namespace

namespace {
class http_connection : public std::enable_shared_from_this<http_connection> {
public:
  http_connection(tcp::socket socket) : socket(std::move(socket)) {}

  void start() { accept_request(); }

private:
  friend class http_request;
  tcp::socket socket;
  beast::flat_buffer buffer{8192};
  http::request<http::string_body> request;
  http::response<http::string_body> response;
  std::optional<http_request> wrapped_request;

  void accept_request() {
    auto self = shared_from_this();
    http::async_read(socket, buffer, request,
                     [self](beast::error_code ec, std::size_t) {
                       if (!ec)
                         self->process_request();
                     });
  }

  void process_request() {
    wrapped_request.emplace(shared_from_this(), request, response);
    g_server->handleRequest(*wrapped_request);
  }

  void writeLog() {
    // https://en.wikipedia.org/wiki/Common_Log_Format

    // There are so many successful requests, writing the log is actually a
    // bottleneck. So let's only log failures.
    if (response.result_int() >= 200 && response.result_int() <= 299)
      return;

    auto ip_address = socket.remote_endpoint().address().to_string();
    auto body_size = response.body().size();

    // TODO: ensure the locale is set correctly.
    char time_buffer[32] = "";
    std::time_t time = std::time(nullptr);
    std::strftime(time_buffer, sizeof(time_buffer), "%d/%b/%Y:%H:%M:%S %z",
                  std::localtime(&time));

    std::lock_guard lock(stdout_mutex);
    std::cout << ip_address << " - - [" << time_buffer << "] \""
              << request.method_string() << " " << request.target() << " HTTP/"
              << (request.version() / 10) << "." << (request.version() % 10)
              << "\" " << response.result_int() << " " << body_size << "\r\n";
  }

  void sendResponse() {
    auto self = shared_from_this();
    wrapped_request->responded = true;
    wrapped_request.reset();
    response.content_length(response.body().size());
    writeLog();
    self->request = {};
    http::async_write(socket, response,
                      [self](beast::error_code ec, std::size_t) {
                        self->response = {};
                        if (!ec)
                          self->accept_request();
                      });
  }
};
} // end anonymous namespace

void http_request::sendBody(const llvm::Twine &body) {
  response.body() = body.str();
  connection->sendResponse();
}

void http_request::sendEmptyBody() {
  response.body().clear();
  connection->sendResponse();
}

static void http_server(tcp::acceptor &acceptor, tcp::socket &socket) {
  acceptor.async_accept(socket, [&](beast::error_code ec) {
    if (!ec)
      std::make_shared<http_connection>(std::move(socket))->start();
    http_server(acceptor, socket);
  });
}

int main(int argc, char **argv) {
  InitTool X(argc, argv);

  cl::HideUnrelatedOptions(server_category);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Server");

  llvm::ExitOnError Err("memodb-server: ");
  auto store = Store::open(GetStoreUri());
  Server server(*store);
  g_server = &server;

  auto uri_or_none = URI::parse(listen_url);
  if (!uri_or_none) {
    llvm::errs() << "Invalid URL: " << listen_url << "\n";
    llvm::errs() << "Try http://127.0.0.1:8000/\n";
    return 1;
  }
  auto const address = net::ip::make_address(uri_or_none->host);
  unsigned short port = static_cast<unsigned short>(uri_or_none->port);
  net::io_context ioc{1}; // may call boost::throw_exception
  tcp::acceptor acceptor{ioc, {address, port}};
  tcp::socket socket{ioc};
  http_server(acceptor, socket);

  // We print this message *after* opening the store (which can take a long
  // time, if database logs need to be replayed).
  llvm::errs() << "Server started!\n";

  ioc.run();
  return 0;
}
