#include "memodb_internal.h"

#if BCDB_WITH_NNG

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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

#include "memodb/CID.h"
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

Response HTTPStore::request(const Twine &method, const Twine &path,
                            const std::optional<Node> &body) {
  auto url = url_parse(base_uri + path);
  auto req = http_req_alloc(url.get());
  auto res = http_res_alloc();
  auto aio = aio_alloc();

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
    checkErr(nng_http_req_set_data(req.get(), bytes.data(), bytes.size()));
  }

  nng_http_conn_transact(getConn(), req.get(), res.get(), aio.get());
  nng_aio_wait(aio.get());
  checkErr(nng_aio_result(aio.get()));

  Response response;
  response.status = nng_http_res_get_status(res.get());
  const char *location = nng_http_res_get_header(res.get(), "Location");
  if (location)
    response.location = location;
  void *res_data;
  size_t res_size;
  nng_http_res_get_data(res.get(), &res_data, &res_size);
  const char *content_type = nng_http_res_get_header(res.get(), "Content-Type");
  if (content_type && StringRef(content_type) == "application/cbor")
    response.body = cantFail(Node::loadFromCBOR(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(res_data), res_size)));
  else
    response.error =
        StringRef(reinterpret_cast<const char *>(res_data), res_size).str();

  return response;
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

#endif // BCDB_WITH_NNG
