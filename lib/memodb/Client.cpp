#include "memodb_internal.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
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
using tcp = net::ip::tcp;
using llvm::ArrayRef;
using llvm::cantFail;
using llvm::DenseMap;
using llvm::raw_svector_ostream;
using llvm::report_fatal_error;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;

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

  http::request<http::vector_body<std::uint8_t>>
  buildRequest(const Twine &method, const Twine &path,
               const std::optional<Node> &body = std::nullopt);
  Response getResponse(http::response<http::vector_body<std::uint8_t>> &res);
  Response request(const Twine &method, const Twine &path,
                   const std::optional<Node> &body = std::nullopt);

  // The base server URI.
  URI base_uri;

  net::io_context ioc;

  // Get the current thread's HTTP connection, creating a new one if necessary.
  beast::tcp_stream &getConn();

  // Used by each thread to look up its own HTTP connection.
  // TODO: entries in this map are never removed, even when the HTTPStore is
  // destroyed, which could cause memory leaks.
  static thread_local DenseMap<HTTPStore *, beast::tcp_stream *>
      thread_connections;

  // Closes all connections in the destructor.
  std::vector<std::unique_ptr<beast::tcp_stream>> open_connections = {};

  // Protects access to open_connections and client.
  std::mutex mutex;
};
} // end anonymous namespace

thread_local DenseMap<HTTPStore *, beast::tcp_stream *>
    HTTPStore::thread_connections =
        DenseMap<HTTPStore *, beast::tcp_stream *>();

beast::tcp_stream &HTTPStore::getConn() {
  beast::tcp_stream *&result = thread_connections[this];
  if (!result) {
    tcp::resolver resolver(ioc);
    auto const port_name = llvm::Twine(base_uri.port).str();
    auto const resolved = resolver.resolve(base_uri.host, port_name);
    auto stream = std::make_unique<beast::tcp_stream>(ioc);
    stream->connect(resolved);
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
  if (!base_uri.path_segments.empty())
    report_fatal_error("HTTP URL must have an empty path");
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

http::request<http::vector_body<std::uint8_t>>
HTTPStore::buildRequest(const Twine &method, const Twine &path,
                        const std::optional<Node> &body) {
  SmallVector<char, 128> buffer;
  auto method_verb = http::string_to_verb(method.toStringRef(buffer));
  if (method_verb == http::verb::unknown)
    report_fatal_error("invalid HTTP method");

  auto target = path.toStringRef(buffer);
  http::request<http::vector_body<std::uint8_t>> request(method_verb, target,
                                                         11);
  request.set(http::field::accept, "application/cbor");
  request.set(http::field::host, base_uri.host);

  if (body) {
    request.body() = body->saveAsCBOR();
    request.set(http::field::content_type, "application/cbor");
    request.prepare_payload();
  }

  return request;
}

Response
HTTPStore::getResponse(http::response<http::vector_body<std::uint8_t>> &res) {
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
  http::write(stream, req);
  beast::flat_buffer buffer;
  http::response<http::vector_body<std::uint8_t>> res;
  http::read(stream, buffer, res);
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
  std::optional<Link> tryEvaluate(const Call &call,
                                  bool inc_started_if_success);
  Link evaluateDeferred(const Call &call, bool work_while_waiting);
  bool workOnce();

  std::unique_ptr<HTTPStore> store;
  llvm::StringMap<std::function<NodeOrCID(Evaluator &, const Call &)>> funcs;
  std::mutex funcs_mutex;
  std::optional<CID> worker_info_cid = std::nullopt;
  std::mutex worker_info_cid_mutex;

  std::vector<std::thread> threads;
  std::atomic<bool> work_done = false;

  // These counters only increase, never decrease.
  std::atomic<unsigned> num_requested = 0, num_started = 0, num_finished = 0;
  std::mutex stderr_mutex;

  void workerThreadImpl();
  void printProgress();
};
} // end anonymous namespace

ClientEvaluator::ClientEvaluator(std::unique_ptr<HTTPStore> store,
                                 unsigned num_threads)
    : store(std::move(store)) {
  threads.reserve(num_threads);
  for (unsigned i = 0; i < num_threads; ++i) {
    threads.emplace_back(&ClientEvaluator::workerThreadImpl, this);
  }
}

ClientEvaluator::~ClientEvaluator() {
  work_done = true;
  // FIXME: cancel in-progress requests from worker threads.
  for (auto &thread : threads)
    thread.join();
}

Store &ClientEvaluator::getStore() { return *store; }

std::optional<Link> ClientEvaluator::tryEvaluate(const Call &call,
                                                 bool inc_started_if_success) {
  // TODO: we need some way to set the timeout parameter.

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
  return evaluateDeferred(call, work_while_waiting);
}

Link ClientEvaluator::evaluateDeferred(const Call &call,
                                       bool work_while_waiting) {
  using namespace std::chrono_literals;
  ++num_started;
  while (true) {
    auto result = tryEvaluate(call, false);
    if (result)
      return *result;

    // Try to do some useful work while waiting for the call. This is
    // especially important if we're the only client connected to the server,
    // and all our threads are in evaluateDeferred().
    //
    // XXX: This can cause stack depth to grow arbitrarily large. If that turns
    // out to be a problem, we may need to change the design.
    if (!(work_while_waiting && workOnce()))
      std::this_thread::sleep_for(1000ms); // TODO: exponential backoff
  }
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
  auto future =
      std::async(std::launch::deferred, &ClientEvaluator::evaluateDeferred,
                 this, call, work_while_waiting);
  return makeFuture(future.share());
}

void ClientEvaluator::registerFunc(
    llvm::StringRef name,
    std::function<NodeOrCID(Evaluator &, const Call &)> func) {
  std::unique_lock lock(funcs_mutex);
  assert(!funcs.count(name) && "duplicate func");
  funcs[name] = std::move(func);

  Node worker_info(node_map_arg, {{"funcs", Node(node_list_arg)}});
  for (const auto &item : funcs)
    worker_info["funcs"].emplace_back(Node(utf8_string_arg, item.getKey()));

  auto cid = store->put(worker_info);
  lock = std::unique_lock(worker_info_cid_mutex);
  worker_info_cid = cid;
}

void ClientEvaluator::printProgress() {
  // Load atomics in this order to avoid getting negative values.
  unsigned finished = num_finished;
  unsigned started = num_started;
  unsigned requested = num_requested;
  llvm::errs() << (requested - started) << " -> " << (started - finished)
               << " -> " << finished;
}

bool ClientEvaluator::workOnce() {
  std::unique_lock lock(worker_info_cid_mutex);
  auto cid = worker_info_cid;
  lock.unlock();
  if (!cid) {
    // No funcs registered yet, so we can't do anything.
    return false;
  }
  auto response = store->request("POST", "/worker", Node(*store, *cid));
  if (response.status < 200 || response.status > 299)
    response.raiseError();
  if (response.body.is_null()) {
    // No jobs available.
    return false;
  }

  Call call("", {});
  call.Name = response.body["func"].as<std::string>();
  for (const auto &arg : response.body["args"].list_range())
    call.Args.emplace_back(arg.as<CID>());
  lock = std::unique_lock(funcs_mutex);
  auto &func = funcs[call.Name];
  lock.unlock();

  std::optional<PrettyStackTraceCall> stack_printer;
  stack_printer.emplace(call);
  Link result(*store, func(*this, call));
  stack_printer.reset();

  SmallVector<char, 256> buffer;
  llvm::raw_svector_ostream os(buffer);
  os << call;
  response = store->request("PUT", os.str(), Node(*store, result.getCID()));
  if (response.status != 201)
    response.raiseError();
  return true;
}

void ClientEvaluator::workerThreadImpl() {
  using namespace std::chrono_literals;
  llvm::PrettyStackTraceString stack_printer("Worker thread (client process)");
  std::this_thread::sleep_for(
      1000ms); // Give the program time to call registerFunc().
  while (!work_done) {
    if (!workOnce())
      std::this_thread::sleep_for(1000ms); // TODO: exponential backoff
  }
}

std::unique_ptr<Evaluator> memodb::createClientEvaluator(llvm::StringRef path,
                                                         unsigned num_threads) {
  auto store = std::make_unique<HTTPStore>();
  store->open(path, false);
  return std::make_unique<ClientEvaluator>(std::move(store), num_threads);
}
