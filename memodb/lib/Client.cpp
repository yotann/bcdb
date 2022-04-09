#include "memodb_internal.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/fiber/all.hpp>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include "memodb/CID.h"
#include "memodb/Evaluator.h"
#include "memodb/Multibase.h"
#include "memodb/Store.h"
#include "memodb/URI.h"

using namespace memodb;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace fibers = boost::fibers;
namespace this_fiber = boost::this_fiber;
using tcp = net::ip::tcp;
namespace local = net::local;
using llvm::ArrayRef;
using llvm::cantFail;
using llvm::DenseMap;
using llvm::raw_svector_ostream;
using llvm::report_fatal_error;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;

using BeastRequest = http::request<http::vector_body<std::uint8_t>>;
using BeastResponse = http::response<http::vector_body<std::uint8_t>>;

namespace {
struct Response {
  unsigned status;
  std::string location;
  Node body;
  std::string error;

  void raiseError();
};
} // end anonymous namespace

namespace {
class Connection {
public:
  virtual ~Connection() {}
  virtual void write(const BeastRequest &req) = 0;
  virtual void read(beast::flat_buffer &buffer, BeastResponse &res) = 0;
};
} // end anonymous namespace

namespace {
template <typename Protocol> class ProtocolConnection : public Connection {
public:
  template <typename Arg>
  ProtocolConnection(net::io_context &ioc, Arg &&remote_endpoint)
      : stream(ioc) {
    stream.connect(remote_endpoint);
  }

  ~ProtocolConnection() override {}

  void write(const BeastRequest &req) override { http::write(stream, req); }

  void read(beast::flat_buffer &buffer, BeastResponse &res) override {
    http::read(stream, buffer, res);
  }

  beast::basic_stream<Protocol> stream;
};
} // end anonymous namespace

namespace {
class HTTPStore : public Store {
public:
  void open(StringRef uri, bool create_if_missing);
  ~HTTPStore() override;

  llvm::Optional<Node> getOptional(const CID &CID) override;
  llvm::Optional<CID> resolveOptional(const Name &Name) override;
  CID put(const Node &value) override;
  void set(const Name &Name, const CID &ref) override;
  std::vector<Name> list_names_using(const CID &ref) override;
  std::vector<std::string> list_funcs() override;
  void eachHead(std::function<bool(const Head &)> F) override;
  void eachCall(StringRef Func, std::function<bool(const Call &)> F) override;
  void head_delete(const Head &Head) override;
  void call_invalidate(StringRef name) override;

private:
  friend struct AsyncRequest;
  friend class ClientEvaluator;

  BeastRequest buildRequest(const Twine &method, const Twine &path,
                            const std::optional<Node> &body = std::nullopt);
  Response getResponse(BeastResponse &res);
  Response request(const Twine &method, const Twine &path,
                   const std::optional<Node> &body = std::nullopt);

  // The base server URI.
  URI base_uri;

  net::io_context ioc;

  // Get the current thread's HTTP connection, creating a new one if necessary.
  Connection &getConn();

  // Used by each thread to look up its own HTTP connection.
  // TODO: entries in this map are never removed, even when the HTTPStore is
  // destroyed, which could cause memory leaks.
  static thread_local DenseMap<HTTPStore *, Connection *> thread_connections;

  // Closes all connections in the destructor.
  std::vector<std::unique_ptr<Connection>> open_connections = {};

  // Protects access to open_connections.
  std::mutex mutex;
};
} // end anonymous namespace

thread_local DenseMap<HTTPStore *, Connection *> HTTPStore::thread_connections =
    DenseMap<HTTPStore *, Connection *>();

Connection &HTTPStore::getConn() {
  Connection *&result = thread_connections[this];
  if (!result) {
    std::unique_ptr<Connection> stream;
    if (base_uri.scheme == "http" || base_uri.scheme == "tcp") {
      if (!base_uri.path_segments.empty())
        report_fatal_error("HTTP URL must have an empty path");
      tcp::resolver resolver(ioc);
      auto const port_name = llvm::Twine(base_uri.port).str();
      auto const resolved = resolver.resolve(base_uri.host, port_name);
      stream = std::make_unique<ProtocolConnection<tcp>>(ioc, resolved);
    } else if (base_uri.scheme == "unix") {
      local::stream_protocol::endpoint endpoint(base_uri.getPathString());
      stream = std::make_unique<ProtocolConnection<local::stream_protocol>>(
          ioc, endpoint);
    } else {
      report_fatal_error("unsupported protocol in URL");
    }
    const std::lock_guard<std::mutex> lock(mutex);
    open_connections.emplace_back(std::move(stream));
    result = open_connections.back().get();
  }
  return *result;
}

void HTTPStore::open(StringRef uri, bool create_if_missing) {
  auto uri_or_none = URI::parse(uri);
  if (!uri_or_none)
    report_fatal_error("invalid HTTP URL");
  base_uri = std::move(*uri_or_none);
  getConn();
}

HTTPStore::~HTTPStore() {}

llvm::Optional<Node> HTTPStore::getOptional(const CID &CID) {
  auto response = request("GET", "/cid/" + CID.asString(Multibase::base64url));
  if (response.status == 404)
    return {};
  if (response.status != 200)
    response.raiseError();
  return std::move(response.body);
}

llvm::Optional<CID> HTTPStore::resolveOptional(const Name &Name) {
  if (const CID *ref = std::get_if<CID>(&Name))
    return *ref;
  SmallVector<char, 128> buffer;
  raw_svector_ostream os(buffer);
  os << Name;
  auto response = request("GET", os.str());
  if (response.status == 404)
    return {};
  if (response.status != 200)
    response.raiseError();
  return response.body.as<CID>();
}

CID HTTPStore::put(const Node &value) {
  auto response = request("POST", "/cid", value);
  if (response.status != 201)
    response.raiseError();
  if (!StringRef(response.location).startswith("/cid/"))
    report_fatal_error("invalid 201 response location");
  return *CID::parse(StringRef(response.location).drop_front(5));
}

void HTTPStore::set(const Name &Name, const CID &ref) {
  if (std::holds_alternative<CID>(Name))
    report_fatal_error("can't set a CID");
  SmallVector<char, 128> buffer;
  raw_svector_ostream os(buffer);
  os << Name;
  auto response = request("PUT", os.str(), Node(*this, ref));
  if (response.status != 201)
    response.raiseError();
}

std::vector<Name> HTTPStore::list_names_using(const CID &ref) {
  return {}; // TODO: unimplemented
}

std::vector<std::string> HTTPStore::list_funcs() {
  auto response = request("GET", "/call");
  if (response.status != 200)
    response.raiseError();
  std::vector<std::string> result;
  for (const auto &item : response.body.list_range()) {
    auto uri = URI::parse(item.as<StringRef>());
    if (!uri || uri->path_segments.size() != 2 ||
        uri->path_segments[0] != "call")
      report_fatal_error("invalid URI in response!");
    result.emplace_back(std::move(uri->path_segments[1]));
  }
  return result;
}

void HTTPStore::eachHead(std::function<bool(const Head &)> F) {
  auto response = request("GET", "/head");
  if (response.status != 200)
    response.raiseError();
  for (const auto &item : response.body.list_range()) {
    auto name = Name::parse(item.as<StringRef>());
    if (!name || !std::holds_alternative<Head>(*name))
      report_fatal_error("invalid URI in response!");
    if (F(std::get<Head>(*name)))
      break;
  }
}

void HTTPStore::eachCall(llvm::StringRef Func,
                         std::function<bool(const Call &)> F) {
  URI func_uri;
  func_uri.path_segments = {"call", Func.str()};
  auto response = request("GET", func_uri.encode());
  if (response.status != 200)
    response.raiseError();
  for (const auto &item : response.body.list_range()) {
    auto name = Name::parse(item.as<StringRef>());
    if (!name || !std::holds_alternative<Call>(*name))
      report_fatal_error("invalid URI in response!");
    if (F(std::get<Call>(*name)))
      break;
  }
}

void HTTPStore::head_delete(const Head &Head) {
  // FIXME
  report_fatal_error("unimplemented");
}

void HTTPStore::call_invalidate(llvm::StringRef name) {
  URI func_uri;
  func_uri.path_segments = {"call", name.str()};
  auto response = request("DELETE", func_uri.encode());
  if (response.status != 204)
    response.raiseError();
}

BeastRequest HTTPStore::buildRequest(const Twine &method, const Twine &path,
                                     const std::optional<Node> &body) {
  SmallVector<char, 128> buffer;
  auto method_verb = http::string_to_verb(method.toStringRef(buffer));
  if (method_verb == http::verb::unknown)
    report_fatal_error("invalid HTTP method");

  auto target = path.toStringRef(buffer);
  BeastRequest request(method_verb, target, 11);
  request.set(http::field::accept, "application/cbor");
  request.set(http::field::host, base_uri.host);

  if (body) {
    request.body() = body->saveAsCBOR();
    request.set(http::field::content_type, "application/cbor");
    request.prepare_payload();
  }

  return request;
}

Response HTTPStore::getResponse(BeastResponse &res) {
  Response response;
  response.status = res.result_int();
  response.location = res[http::field::location];
  void *res_data = res.body().data();
  size_t res_size = res.body().size();
  if (res[http::field::content_type] == "application/cbor")
    response.body = cantFail(Node::loadFromCBOR(
        *this, ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(res_data),
                                 res_size)));
  else
    response.error =
        StringRef(reinterpret_cast<const char *>(res_data), res_size).str();
  return response;
}

Response HTTPStore::request(const Twine &method, const Twine &path,
                            const std::optional<Node> &body) {
  auto &stream = getConn();
  auto req = buildRequest(method, path, body);
  beast::flat_buffer buffer;
  BeastResponse res;
  // XXX: There must not be any fiber switches (fibers::mutex::lock(),
  // this_fiber::sleep_for(), etc.) between the calls to http::write() and
  // http::read(). We use 1 HTTP connection per thread, and this ensures that
  // each thread makes only 1 HTTP request at a time.
  stream.write(req);
  stream.read(buffer, res);
  return getResponse(res);
}

void Response::raiseError() {
  report_fatal_error("Error response " + Twine(status) + ": " + error);
}

std::unique_ptr<Store> memodb_http_open(llvm::StringRef path,
                                        bool create_if_missing) {
  auto store = std::make_unique<HTTPStore>();
  store->open(path, create_if_missing);
  return store;
}

namespace {
class CountingSemaphore {
public:
  explicit CountingSemaphore(unsigned desired) : count(desired) {}

  void acquire() {
    std::unique_lock lock(mutex);
    cv.wait(lock, [this]() { return cancelled || count != 0; });
    if (!cancelled)
      count--;
  }

  void release(unsigned update = 1) {
    {
      auto lock = std::unique_lock(mutex);
      count += update;
    }
    cv.notify_one();
  }

  void cancel() {
    cancelled = true;
    cv.notify_all();
  }

  bool isCancelled() const { return cancelled; }

private:
  std::atomic<bool> cancelled = false;
  int count = 0;
  fibers::mutex mutex;
  fibers::condition_variable cv;
};
} // end anonymous namespace

namespace {
class ClientEvaluator : public Evaluator {
public:
  ClientEvaluator(std::unique_ptr<HTTPStore> store, unsigned num_threads);
  ~ClientEvaluator() override;
  Store &getStore() override;
  Link evaluate(const Call &call, bool work_while_waiting = true) override;
  Future evaluateAsync(const Call &call,
                       bool work_while_waiting = true) override;
  void registerFunc(
      llvm::StringRef name,
      std::function<NodeOrCID(Evaluator &, const Call &)> func) override;

private:
  void updateWorkerInfo();
  std::optional<Link> tryEvaluate(const Call &call,
                                  bool inc_started_if_success);
  Link evaluateDeferred(const Call &call);

  std::unique_ptr<HTTPStore> store;
  llvm::StringMap<std::function<NodeOrCID(Evaluator &, const Call &)>> funcs;
  bool funcs_changed = false;
  fibers::mutex funcs_mutex;
  std::optional<CID> worker_info_cid;

  std::vector<std::thread> worker_threads;
  CountingSemaphore work_semaphore;

  // These counters only increase, never decrease.
  std::atomic<unsigned> num_requested = 0, num_started = 0, num_finished = 0;
  fibers::mutex stderr_mutex;

  void workerThreadImpl(unsigned num_threads);
  void printProgress();
};
} // end anonymous namespace

ClientEvaluator::ClientEvaluator(std::unique_ptr<HTTPStore> store,
                                 unsigned num_threads)
    : store(std::move(store)), work_semaphore(num_threads) {
  worker_threads.reserve(num_threads);
  for (unsigned i = 0; i < num_threads; ++i) {
    worker_threads.emplace_back(&ClientEvaluator::workerThreadImpl, this,
                                num_threads);
  }
}

ClientEvaluator::~ClientEvaluator() {
  work_semaphore.cancel();
  // FIXME: cancel in-progress requests from threads.
  for (auto &thread : worker_threads)
    thread.join();
}

Store &ClientEvaluator::getStore() { return *store; }

std::optional<Link> ClientEvaluator::tryEvaluate(const Call &call,
                                                 bool inc_started_if_success) {
  SmallVector<char, 256> buffer;
  llvm::raw_svector_ostream os(buffer);
  os << call << "/evaluate";

  Response response;

  response = store->request("POST", os.str());
  if (response.status == 202) {
    // No result yet.
    return std::nullopt;
  } else if (response.status != 200) {
    response.raiseError();
  }

  if (inc_started_if_success)
    ++num_started;
  ++num_finished;
  if (auto stderr_lock = std::unique_lock(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " finished " << call << "\n";
  }

  return Link(*store, response.body.as<CID>());
}

Link ClientEvaluator::evaluate(const Call &call, bool work_while_waiting) {
  ++num_requested;
  if (auto stderr_lock = std::unique_lock(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " starting " << call << "\n";
  }
  return evaluateDeferred(call);
}

Link ClientEvaluator::evaluateDeferred(const Call &call) {
  using namespace std::chrono_literals;
  ++num_started;
  work_semaphore.release();
  std::optional<Link> result;
  while (!(result = tryEvaluate(call, false)))
    this_fiber::sleep_for(1000ms); // TODO: exponential backoff
  work_semaphore.acquire();
  return *result;
}

Future ClientEvaluator::evaluateAsync(const Call &call,
                                      bool work_while_waiting) {
  ++num_requested;
  if (auto stderr_lock = std::unique_lock(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " starting " << call << "\n";
  }
  auto early_result = tryEvaluate(call, true);
  if (early_result) {
    std::promise<Link> promise;
    promise.set_value(*early_result);
    return makeFuture(promise.get_future().share());
  }
  auto future = std::async(std::launch::deferred,
                           &ClientEvaluator::evaluateDeferred, this, call);
  return makeFuture(future.share());
}

void ClientEvaluator::registerFunc(
    llvm::StringRef name,
    std::function<NodeOrCID(Evaluator &, const Call &)> func) {
  std::unique_lock lock(funcs_mutex);
  assert(!funcs.count(name) && "duplicate func");
  funcs[name] = std::move(func);
  funcs_changed = true;
}

void ClientEvaluator::printProgress() {
  // Load atomics in this order to avoid getting negative values.
  unsigned finished = num_finished;
  unsigned started = num_started;
  unsigned requested = num_requested;
  llvm::errs() << (requested - started) << " -> " << (started - finished)
               << " -> " << finished;
}

void ClientEvaluator::updateWorkerInfo() {
  std::unique_lock lock(funcs_mutex);
  if (funcs_changed) {
    Node worker_info(node_map_arg, {{"funcs", Node(node_list_arg)}});
    for (const auto &item : funcs)
      worker_info["funcs"].emplace_back(Node(utf8_string_arg, item.getKey()));
    worker_info_cid = store->put(worker_info);
  }
}

void ClientEvaluator::workerThreadImpl(unsigned num_threads) {
  using namespace std::chrono_literals;

  // work_stealing would be faster, but it busywaits if there's nothing to do,
  // which is undesirable.
  //
  // TODO: is it worth writing our own work stealing algorithm that avoids
  // busywaiting? Keeping track of priorities would also be nice; we could
  // prioritize existing jobs over new ones, and potentially eliminate the need
  // for work_semaphore.
  if (num_threads > 1)
    fibers::use_scheduling_algorithm<fibers::algo::shared_work>(
        /*suspend*/ true);

  while (true) {
    work_semaphore.acquire();
    if (work_semaphore.isCancelled())
      return;

    Response response;
    while (true) {
      updateWorkerInfo();
      if (work_semaphore.isCancelled())
        return;
      if (worker_info_cid) {
        response =
            store->request("POST", "/worker", Node(*store, *worker_info_cid));
        if (response.status < 200 || response.status > 299)
          response.raiseError();
      }
      if (!response.body.is_null())
        break;
      // No jobs available, or worker_info_cid is nullopt (no funcs
      // registered yet).
      this_fiber::sleep_for(1000ms); // TODO: exponential backoff
    }

    Call call("", {});
    call.Name = response.body["func"].as<std::string>();
    for (const auto &arg : response.body["args"].list_range())
      call.Args.emplace_back(arg.as<CID>());

    fibers::fiber([this, call]() {
      // FIXME: doesn't work with fibers
      // PrettyStackTraceCall stack_printer(call);
      std::function<NodeOrCID(Evaluator &, const Call &)> *func;
      {
        std::unique_lock lock(funcs_mutex);
        func = &funcs[call.Name];
      }
      Link result(*store, (*func)(*this, call));
      SmallVector<char, 256> buffer;
      llvm::raw_svector_ostream os(buffer);
      os << call;
      auto response =
          store->request("PUT", os.str(), Node(*store, result.getCID()));
      if (response.status != 201)
        response.raiseError();
      work_semaphore.release();
    }).detach();
  }
}

std::unique_ptr<Evaluator> memodb::createClientEvaluator(llvm::StringRef path,
                                                         unsigned num_threads) {
  auto store = std::make_unique<HTTPStore>();
  store->open(path, false);
  return std::make_unique<ClientEvaluator>(std::move(store), num_threads);
}
