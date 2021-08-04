#include "memodb_internal.h"

#if BCDB_WITH_NNG

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>
#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/util/platform.h>

#include "memodb/CID.h"
#include "memodb/Evaluator.h"
#include "memodb/Multibase.h"
#include "memodb/Store.h"
#include "memodb/URI.h"

using namespace memodb;
using llvm::ArrayRef;
using llvm::cantFail;
using llvm::DenseMap;
using llvm::raw_svector_ostream;
using llvm::report_fatal_error;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::Twine;

namespace {

struct AioDeleter {
  void operator()(nng_aio *aio) { nng_aio_free(aio); }
};

struct HTTPClientDeleter {
  void operator()(nng_http_client *client) { nng_http_client_free(client); }
};

struct HTTPConnCloser {
  void operator()(nng_http_conn *conn) { nng_http_conn_close(conn); }
};

struct HTTPRequestDeleter {
  void operator()(nng_http_req *req) { nng_http_req_free(req); }
};

struct HTTPResponseDeleter {
  void operator()(nng_http_res *res) { nng_http_res_free(res); }
};

struct URLDeleter {
  void operator()(nng_url *url) { nng_url_free(url); }
};

}; // end anonymous namespace

static void checkErr(int err) {
  if (err)
    llvm::report_fatal_error(nng_strerror(err));
}

static std::unique_ptr<nng_url, URLDeleter> url_parse(const Twine &str) {
  SmallVector<char, 256> buffer;
  nng_url *result;
  checkErr(
      nng_url_parse(&result, str.toNullTerminatedStringRef(buffer).data()));
  return std::unique_ptr<nng_url, URLDeleter>(result);
}

static std::unique_ptr<nng_aio, AioDeleter> aio_alloc() {
  nng_aio *result;
  checkErr(nng_aio_alloc(&result, nullptr, nullptr));
  return std::unique_ptr<nng_aio, AioDeleter>(result);
}

static std::unique_ptr<nng_aio, AioDeleter> aio_alloc(void (*callb)(void *),
                                                      void *arg) {
  nng_aio *result;
  checkErr(nng_aio_alloc(&result, callb, arg));
  return std::unique_ptr<nng_aio, AioDeleter>(result);
}

static std::unique_ptr<nng_http_client, HTTPClientDeleter>
http_client_alloc(nng_url *url) {
  nng_http_client *result;
  checkErr(nng_http_client_alloc(&result, url));
  return std::unique_ptr<nng_http_client, HTTPClientDeleter>(result);
}

static std::unique_ptr<nng_http_req, HTTPRequestDeleter>
http_req_alloc(const nng_url *url) {
  nng_http_req *result;
  checkErr(nng_http_req_alloc(&result, url));
  return std::unique_ptr<nng_http_req, HTTPRequestDeleter>(result);
}

static std::unique_ptr<nng_http_res, HTTPResponseDeleter> http_res_alloc() {
  nng_http_res *result;
  checkErr(nng_http_res_alloc(&result));
  return std::unique_ptr<nng_http_res, HTTPResponseDeleter>(result);
}

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

  std::unique_ptr<nng_http_req, HTTPRequestDeleter>
  buildRequest(const Twine &method, const Twine &path,
               const std::optional<Node> &body = std::nullopt);
  Response getResponse(nng_http_res *res);
  Response request(const Twine &method, const Twine &path,
                   const std::optional<Node> &body = std::nullopt);

  // The base server URI, without a trailing slash;
  std::string base_uri;

  // Get the current thread's HTTP connection, creating a new one if necessary.
  nng_http_conn *getConn();

  // Used by each thread to look up its own HTTP connection.
  // TODO: entries in this map are never removed, even when the HTTPStore is
  // destroyed, which could cause memory leaks.
  static thread_local DenseMap<HTTPStore *, nng_http_conn *> thread_connections;

  // Used to make new connections to the server.
  std::unique_ptr<nng_http_client, HTTPClientDeleter> client;

  // Closes all connections in the destructor.
  std::vector<std::unique_ptr<nng_http_conn, HTTPConnCloser>> open_connections =
      {};

  // Protects access to open_connections and client.
  std::mutex mutex;
};
} // end anonymous namespace

thread_local DenseMap<HTTPStore *, nng_http_conn *>
    HTTPStore::thread_connections = DenseMap<HTTPStore *, nng_http_conn *>();

nng_http_conn *HTTPStore::getConn() {
  nng_http_conn *&result = thread_connections[this];
  if (!result) {
    auto aio = aio_alloc();
    const std::lock_guard<std::mutex> lock(mutex);
    nng_http_client_connect(client.get(), aio.get());
    nng_aio_wait(aio.get());
    checkErr(nng_aio_result(aio.get()));
    result = static_cast<nng_http_conn *>(nng_aio_get_output(aio.get(), 0));
    open_connections.emplace_back(result);
  }
  return result;
}

void HTTPStore::open(StringRef uri, bool create_if_missing) {
  base_uri = uri.str();
  if (!base_uri.empty() && base_uri.back() == '/')
    base_uri.pop_back();
  auto url = url_parse(uri);
  client = http_client_alloc(url.get());
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
  auto response = request("PUT", os.str(), Node(ref));
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

std::unique_ptr<nng_http_req, HTTPRequestDeleter>
HTTPStore::buildRequest(const Twine &method, const Twine &path,
                        const std::optional<Node> &body) {
  auto url = url_parse(base_uri + path);
  auto req = http_req_alloc(url.get());
  SmallVector<char, 128> buffer;
  checkErr(nng_http_req_set_method(
      req.get(), method.toNullTerminatedStringRef(buffer).data()));
  checkErr(nng_http_req_set_uri(req.get(),
                                path.toNullTerminatedStringRef(buffer).data()));
  checkErr(nng_http_req_set_header(req.get(), "Accept", "application/cbor"));
  std::vector<uint8_t> bytes;
  if (body) {
    body->save_cbor(bytes);
    checkErr(
        nng_http_req_set_header(req.get(), "Content-Type", "application/cbor"));
    checkErr(nng_http_req_copy_data(req.get(), bytes.data(), bytes.size()));
  }
  return req;
}

Response HTTPStore::getResponse(nng_http_res *res) {
  Response response;
  response.status = nng_http_res_get_status(res);
  const char *location = nng_http_res_get_header(res, "Location");
  if (location)
    response.location = location;
  void *res_data;
  size_t res_size;
  nng_http_res_get_data(res, &res_data, &res_size);
  const char *content_type = nng_http_res_get_header(res, "Content-Type");
  if (content_type && StringRef(content_type) == "application/cbor")
    response.body = cantFail(Node::loadFromCBOR(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(res_data), res_size)));
  else
    response.error =
        StringRef(reinterpret_cast<const char *>(res_data), res_size).str();
  return response;
}

Response HTTPStore::request(const Twine &method, const Twine &path,
                            const std::optional<Node> &body) {
  auto res = http_res_alloc();
  auto aio = aio_alloc();
  auto req = buildRequest(method, path, body);
  nng_http_conn_transact(getConn(), req.get(), res.get(), aio.get());
  nng_aio_wait(aio.get());
  checkErr(nng_aio_result(aio.get()));
  return getResponse(res.get());
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
  NodeRef evaluate(const Call &call) override;
  Future evaluateAsync(const Call &call) override;
  void registerFunc(
      llvm::StringRef name,
      std::function<NodeOrCID(Evaluator &, const Call &)> func) override;

private:
  friend struct AsyncRequest;

  std::unique_ptr<HTTPStore> store;
  llvm::StringMap<std::function<NodeOrCID(Evaluator &, const Call &)>> funcs;
  std::mutex funcs_mutex;
  std::optional<CID> worker_info_cid = std::nullopt;
  std::mutex worker_info_cid_mutex;

  std::vector<std::thread> threads;
  std::vector<std::unique_ptr<nng_aio, AioDeleter>> thread_aios;
  std::atomic<bool> work_done = false;

  // These counters only increase, never decrease.
  std::atomic<unsigned> num_requested = 0, num_started = 0, num_finished = 0;
  std::mutex stderr_mutex;

  void workerThreadImpl(nng_aio *aio);

  void printProgress();
};
} // end anonymous namespace

ClientEvaluator::ClientEvaluator(std::unique_ptr<HTTPStore> store,
                                 unsigned num_threads)
    : store(std::move(store)) {
  threads.reserve(num_threads);
  thread_aios.reserve(num_threads);
  for (unsigned i = 0; i < num_threads; ++i) {
    thread_aios.emplace_back(aio_alloc());
    threads.emplace_back(&ClientEvaluator::workerThreadImpl, this,
                         thread_aios.back().get());
  }
}

ClientEvaluator::~ClientEvaluator() {
  work_done = true;
  // FIXME: cancel in-progress requests from worker threads.
  for (auto &thread : threads)
    thread.join();
}

Store &ClientEvaluator::getStore() { return *store; }

NodeRef ClientEvaluator::evaluate(const Call &call) {
  // TODO: we need some way to set the timeout parameter.

  SmallVector<char, 256> buffer;
  llvm::raw_svector_ostream os(buffer);
  os << call << "/evaluate";

  // TODO: get rid of duplicate code in AsyncRequest.

  Response response;
  bool accepted = false;
  ++num_requested;

  // Use try_to_lock so that printing to stderr doesn't become a bottleneck. If
  // there are multiple threads, messages may be skipped, but if the thread
  // pool is empty and Evaluator is only used by one thread, all messages will
  // be printed.
  if (auto stderr_lock = std::unique_lock(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " starting " << call << "\n";
  }

  while (true) {
    response = store->request("POST", os.str());
    if (response.status == 202) {
      // Accepted
      if (!accepted) {
        accepted = true;
        ++num_started;
      }
      continue;
    } else if (response.status == 503) {
      // Service Unavailable
      continue;
    } else if (response.status == 200) {
      // OK
      break;
    } else {
      response.raiseError();
    }
  }
  if (!accepted) {
    accepted = true;
    ++num_started;
  }

  ++num_finished;
  if (auto stderr_lock = std::unique_lock(stderr_mutex, std::try_to_lock)) {
    printProgress();
    llvm::errs() << " finished " << call << "\n";
  }

  return NodeRef(*store, response.body.as<CID>());
}

namespace {
struct AsyncRequest {
  AsyncRequest(ClientEvaluator *evaluator, Call call);
  void start();
  void callback();

  ClientEvaluator *evaluator;
  Call call;
  std::promise<NodeRef> promise;
  std::unique_ptr<nng_http_conn, HTTPConnCloser> conn;
  std::unique_ptr<nng_http_req, HTTPRequestDeleter> req;
  std::unique_ptr<nng_http_res, HTTPResponseDeleter> res;
  std::unique_ptr<nng_aio, AioDeleter> aio;
  bool accepted = false;
};
} // end anonymous namespace

static void requestHandler(void *arg) {
  static_cast<AsyncRequest *>(arg)->callback();
}

AsyncRequest::AsyncRequest(ClientEvaluator *evaluator, Call call)
    : evaluator(evaluator), call(call) {
  SmallVector<char, 256> buffer;
  llvm::raw_svector_ostream os(buffer);
  os << call << "/evaluate";

  aio = aio_alloc(requestHandler, this);
  req = evaluator->store->buildRequest("POST", os.str());
  res = http_res_alloc();

  nng_http_client_connect(evaluator->store->client.get(), aio.get());
}

void AsyncRequest::start() {
  nng_http_conn_transact(conn.get(), req.get(), res.get(), aio.get());
}

void AsyncRequest::callback() {
  checkErr(nng_aio_result(aio.get()));
  if (!conn) {
    conn.reset(static_cast<nng_http_conn *>(nng_aio_get_output(aio.get(), 0)));
    ++evaluator->num_requested;
    if (auto stderr_lock = std::unique_lock(evaluator->stderr_mutex,
                                                        std::try_to_lock)) {
      evaluator->printProgress();
      llvm::errs() << " starting " << call << "\n";
    }
    start();
    return;
  }
  auto response = evaluator->store->getResponse(res.get());
  if (response.status == 202) {
    // Accepted
    if (!accepted) {
      accepted = true;
      ++evaluator->num_started;
    }
    if (auto stderr_lock =
            std::unique_lock(evaluator->stderr_mutex, std::try_to_lock)) {
      evaluator->printProgress();
      llvm::errs() << " awaiting " << call << "\n";
    }
    start();
  } else if (response.status == 503) {
    // Service Unavailable
    if (auto stderr_lock =
            std::unique_lock(evaluator->stderr_mutex, std::try_to_lock)) {
      evaluator->printProgress();
      llvm::errs() << " retrying " << call << "\n";
    }
    start();
  } else if (response.status == 200) {
    // OK
    if (!accepted) {
      accepted = true;
      ++evaluator->num_started;
    }
    ++evaluator->num_finished;
    if (auto stderr_lock =
            std::unique_lock(evaluator->stderr_mutex, std::try_to_lock)) {
      evaluator->printProgress();
      llvm::errs() << " finished " << call << "\n";
    }
    promise.set_value(NodeRef(*evaluator->store, response.body.as<CID>()));
    // We can't delete aio here because that NNG would deadlock.
    // FIXME: this leaks the aio. Need to think of a better solution.
    aio.release();
    delete this;
  } else {
    response.raiseError();
  }
}

Future ClientEvaluator::evaluateAsync(const Call &call) {
  // TODO: we need some way to set the timeout parameter.
  AsyncRequest *request = new AsyncRequest(this, call);
  return makeFuture(request->promise.get_future().share());
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

void ClientEvaluator::workerThreadImpl(nng_aio *aio) {
  nng_msleep(1000); // Give the program time to call registerFunc().
  while (!work_done) {
    std::unique_lock lock(worker_info_cid_mutex);
    auto cid = worker_info_cid;
    lock.unlock();
    if (!cid) {
      // No funcs registered yet, so we can't do anything.
      nng_msleep(1000);
      continue;
    }
    auto res = http_res_alloc();
    auto req = store->buildRequest("POST", "/worker", Node(*cid));
    nng_http_conn_transact(store->getConn(), req.get(), res.get(), aio);
    nng_aio_wait(aio);
    if (nng_aio_result(aio) == NNG_ECANCELED)
      continue;
    checkErr(nng_aio_result(aio));
    auto response = store->getResponse(res.get());
    if (response.status < 200 || response.status > 299)
      response.raiseError();
    if (response.body.is_null())
      continue; // no jobs available

    Call call("", {});
    call.Name = response.body["func"].as<std::string>();
    for (const auto &arg : response.body["args"].list_range())
      call.Args.emplace_back(arg.as<CID>());
    lock = std::unique_lock(funcs_mutex);
    auto &func = funcs[call.Name];
    lock.unlock();
    auto result = NodeRef(*store, func(*this, call));

    SmallVector<char, 256> buffer;
    llvm::raw_svector_ostream os(buffer);
    os << call;
    response = store->request("PUT", os.str(), Node(result.getCID()));
    if (response.status != 201)
      response.raiseError();
  }
}

std::unique_ptr<Evaluator> memodb::createClientEvaluator(llvm::StringRef path,
                                                         unsigned num_threads) {
  auto store = std::make_unique<HTTPStore>();
  store->open(path, false);
  return std::make_unique<ClientEvaluator>(std::move(store), num_threads);
}

#else // BCDB_WITH_NNG

#include <memory>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

using namespace memodb;

std::unique_ptr<Store> memodb_http_open(llvm::StringRef path,
                                        bool create_if_missing) {
  llvm::report_fatal_error(
      "MemoDB was compiled without HTTP support (requires NNG)");
}

std::unique_ptr<Evaluator> memodb::createClientEvaluator(llvm::StringRef path,
                                                         unsigned num_threads) {
  llvm::report_fatal_error(
      "MemoDB was compiled without HTTP support (requires NNG)");
}

#endif // BCDB_WITH_NNG
