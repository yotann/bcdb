// This file is based on the example code here:
// https://www.boost.org/doc/libs/1_78_0/libs/beast/example/advanced/server/advanced_server.cpp

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <boost/asio.hpp>
#include <boost/assert/source_location.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Threading.h>

#include "memodb/HTTP.h"
#include "memodb/Request.h"
#include "memodb/Server.h"
#include "memodb/Store.h"
#include "memodb/ToolSupport.h"
#include "memodb/URI.h"

using namespace memodb;
namespace cl = llvm::cl;
using llvm::Optional;
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

// Note: this doesn't affect the number of RocksDB threads.
static cl::opt<std::string>
    threads_option("j", cl::desc("Number of server threads, or \"all\""),
                   cl::cat(server_category), cl::sub(*cl::AllSubCommands));

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

namespace {
template <class Send> class BeastHTTPRequest : public HTTPRequest {
public:
  BeastHTTPRequest(Send &send, http::request<http::string_body> &&request)
      : HTTPRequest(request.method_string(), URI::parse(request.target())),
        send(send), request(std::move(request)) {
    response.version(request.version());
  }

  ~BeastHTTPRequest() override {}

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

  void sendBody(const llvm::Twine &body) override {
    response.body() = body.str();
    responded = true;
    response.content_length(response.body().size());
    send(std::move(response), request);
  }

  void sendEmptyBody() override { sendBody(""); }

private:
  Send &send;
  http::request<http::string_body> request;
  http::response<http::string_body> response;
};
} // end anonymous namespace

template <typename T>
static void writeEndpoint(std::ostream &os, const T &endpoint) {
  os << "-";
}

static void writeEndpoint(std::ostream &os, const tcp::endpoint &endpoint) {
  os << endpoint.address();
}

template <class Send>
static void handleRequest(Server &server,
                          http::request<http::string_body> &&req, Send &send) {
  // NOTE: Boost's example code uses "Send &&" instead of "Send &". This is
  // incorrect because, if the Send (which is actually a HTTPSession::Queue) is
  // moved into the BeastHTTPRequest, its contents will be deleted too soon.
  BeastHTTPRequest request_class(send, std::move(req));
  server.handleRequest(request_class);
}

namespace {
template <typename Protocol>
class HTTPSession : public std::enable_shared_from_this<HTTPSession<Protocol>> {
  class Queue {
    enum {
      // Maximum number of responses we will queue.
      limit = 8
    };

    // The type-erased, saved work item.
    struct Work {
      virtual ~Work() = default;
      virtual void operator()() = 0;
    };

    HTTPSession<Protocol> &self;
    std::vector<std::unique_ptr<Work>> items;

  public:
    explicit Queue(HTTPSession<Protocol> &self) : self(self) {
      static_assert(limit > 0, "Queue limit must be positive");
      items.reserve(limit);
    }

    bool isFull() const { return items.size() >= limit; }

    bool onWrite() {
      assert(!items.empty());
      auto const was_full = isFull();
      items.erase(items.begin());
      if (!items.empty())
        (*items.front())();
      return was_full;
    }

    void operator()(http::response<http::string_body> &&msg,
                    const http::request<http::string_body> &req) {
      struct WorkImpl : Work {
        HTTPSession<Protocol> &self;
        http::response<http::string_body> msg;

        WorkImpl(HTTPSession<Protocol> &self,
                 http::response<http::string_body> &&msg)
            : self(self), msg(std::move(msg)) {}

        void operator()() override {
          auto self_local = self.shared_from_this();
          bool close = msg.need_eof();
          http::async_write(self.stream, msg,
                            [self_local, close](beast::error_code ec,
                                                std::size_t bytes_transferred) {
                              self_local->onWrite(close, ec, bytes_transferred);
                            });
        }
      };

      self.writeLog(msg, req);
      items.push_back(std::make_unique<WorkImpl>(self, std::move(msg)));

      if (items.size() == 1)
        (*items.front())();
    }
  };

  beast::basic_stream<Protocol> stream;
  beast::flat_buffer buffer;
  Server &server;
  Queue queue;

  std::optional<http::request_parser<http::string_body>> parser;

public:
  HTTPSession(typename Protocol::socket &&socket, Server &server)
      : stream(std::move(socket)), server(server), queue(*this) {}

  void run() {
    auto self = this->shared_from_this();
    net::dispatch(stream.get_executor(), [self]() { self->doRead(); });
  }

private:
  void doRead() {
    parser.emplace();
    stream.expires_after(std::chrono::seconds(300));
    auto self = this->shared_from_this();
    http::async_read(
        stream, buffer, *parser,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
          self->onRead(ec, bytes_transferred);
        });
  }

  void onRead(beast::error_code ec, std::size_t bytes_transferred) {
    (void)bytes_transferred;
    if (ec == http::error::end_of_stream)
      return doClose();
    if (ec) {
      // Disabled because it prints "Connection reset by peer" frequently.
      // std::cerr << "read: " << ec.message() << "\n";
      return;
    }
    handleRequest(server, parser->release(), queue);
    if (!queue.isFull())
      doRead();
  }

  void onWrite(bool close, beast::error_code ec,
               std::size_t bytes_transferred) {
    (void)bytes_transferred;
    if (ec) {
      // Disabled because it prints "broken pipe" for every request.
      // std::cerr << "write: " << ec.message() << "\n";
      return;
    }
    if (close)
      return doClose();
    if (queue.onWrite())
      doRead();
  }

  void doClose() {
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_send, ec);
  }

  void writeLog(const http::response<http::string_body> &response,
                const http::request<http::string_body> &request) {
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
    writeEndpoint(std::cout, stream.socket().local_endpoint());
    std::cout << " - - [" << time_buffer << "] \"" << request.method_string()
              << " " << request.target() << " HTTP/" << (request.version() / 10)
              << "." << (request.version() % 10) << "\" "
              << response.result_int() << " " << body_size << "\r\n";
  }
};
} // namespace

namespace {
template <typename Protocol>
class Listener : public std::enable_shared_from_this<Listener<Protocol>> {
  net::io_context &ioc;
  typename Protocol::acceptor acceptor;
  Server &server;

public:
  Listener(net::io_context &ioc, typename Protocol::endpoint endpoint,
           Server &server)
      : ioc(ioc), acceptor(net::make_strand(ioc)), server(server) {
    acceptor.open(endpoint.protocol());
    acceptor.set_option(net::socket_base::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen(net::socket_base::max_listen_connections);
  }

  void run() {
    auto self = this->shared_from_this();
    net::dispatch(acceptor.get_executor(), [self]() { self->doAccept(); });
  }

private:
  void doAccept() {
    auto self = this->shared_from_this();
    acceptor.async_accept(
        net::make_strand(ioc),
        [self](beast::error_code ec, typename Protocol::socket socket) {
          self->onAccept(ec, std::move(socket));
        });
  }

  void onAccept(beast::error_code ec, typename Protocol::socket socket) {
    if (!ec)
      std::make_shared<HTTPSession<Protocol>>(std::move(socket), server)->run();
    doAccept();
  }
};
} // end anonymous namespace

int main(int argc, char **argv) {
  InitTool X(argc, argv);
  cl::HideUnrelatedOptions(server_category);
  cl::ParseCommandLineOptions(argc, argv, "MemoDB Server");

  // We open the store *before* printing anything or opening a socket. Opening
  // the store can take a long time if database logs need to be replayed.
  auto store = Store::open(GetStoreUri());

  // Create the protocol-agnostic Server instance.
  Server server(*store);

  int thread_count;
  Optional<llvm::ThreadPoolStrategy> strategy_or_none =
      llvm::get_threadpool_strategy(threads_option);
  if (!strategy_or_none)
    llvm::report_fatal_error("invalid number of threads");
  thread_count = strategy_or_none->compute_thread_count();
  thread_count = std::max(thread_count, 1);
  if (thread_count == 0)
    llvm::report_fatal_error("invalid number of threads");

  net::io_context ioc{thread_count};

  // Create and launch a listening port.
  auto uri_or_none = URI::parse(listen_url);
  if (!uri_or_none) {
    llvm::errs() << "Invalid URL: " << listen_url << "\n";
    llvm::errs() << "Try http://127.0.0.1:8000/\n";
    return 1;
  }
  if (uri_or_none->scheme == "http" || uri_or_none->scheme == "tcp") {
    auto const address = net::ip::make_address(uri_or_none->host);
    unsigned short port = static_cast<unsigned short>(uri_or_none->port);
    std::make_shared<Listener<tcp>>(ioc, tcp::endpoint{address, port}, server)
        ->run();
  } else if (uri_or_none->scheme == "unix") {
    std::make_shared<Listener<local::stream_protocol>>(
        ioc, uri_or_none->getPathString(), server)
        ->run();
  } else {
    llvm::errs() << "Invalid scheme: " << uri_or_none->scheme << "\n";
    llvm::errs() << "Use http, tcp, or unix\n";
    return 1;
  }

  // TODO: do we need to capture SIGINT/SIGTERM like the example code?

  // Run the I/O service on the requested number of threads.
  std::vector<std::thread> threads;
  threads.reserve(thread_count - 1);
  for (int i = 0; i < thread_count - 1; ++i) {
    threads.emplace_back([&ioc] { ioc.run(); });
  }
  llvm::errs() << "Server started!\n";
  ioc.run();

  // It's impossible to get here because we never call ioc.stop().

  for (auto &thread : threads)
    thread.join();

  return 0;
}
