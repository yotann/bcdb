#ifndef MEMODB_NNG_H
#define MEMODB_NNG_H

// This is a C++ wrapper for the NNG library, like nngpp, except that it
// doesn't use exceptions. This is important if the LLVM build we're using had
// exceptions disabled, as some of the official builds do.

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/Error.h>
#include <memory>
#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/util/platform.h>
#include <optional>

namespace memodb {
namespace nng {

class ErrorInfo : public llvm::ErrorInfo<ErrorInfo> {
public:
  static char ID;

  ErrorInfo(int err, const char *function_name)
      : err(err), function_name(function_name) {
    assert(err != 0 && "Error must not be constructed for success");
  }

  void log(llvm::raw_ostream &os) const override {
    os << function_name << ": " << nng_strerror(err);
  }

  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

  bool isClosed() const { return err == NNG_ECLOSED; }

  bool isCanceled() const { return err == NNG_ECANCELED; }

private:
  int err;
  const char *function_name;
};

inline void msleep(nng_duration msec) { nng_msleep(msec); }

namespace detail {

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

}; // end namespace detail

class URL {
private:
  std::unique_ptr<nng_url, detail::URLDeleter> url;

public:
  URL() = default;
  URL(URL &&rhs) noexcept = default;
  URL &operator=(URL &&rhs) = default;
  explicit URL(nng_url *url) noexcept : url(url) {}
  URL(const URL &rhs) { *this = rhs; }
  URL &operator=(const URL &rhs) {
    nng_url *result;
    int err = nng_url_clone(&result, rhs.url.get());
    if (err != 0)
      llvm::report_fatal_error("nng_url_clone out of memory");
    url.reset(result);
    return *this;
  }
  explicit operator bool() const { return static_cast<bool>(url); }
  nng_url *get() const { return url.get(); }

  static llvm::Expected<URL> parse(llvm::StringRef str) {
    nng_url *result;
    int err = nng_url_parse(&result, str.str().c_str());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_url_parse");
    return URL(result);
  }

  llvm::StringRef getRawURL() const { return url->u_rawurl; }

  llvm::StringRef getScheme() const { return url->u_scheme; }

  std::optional<llvm::StringRef> getUserInfo() const {
    if (!url->u_userinfo)
      return std::nullopt;
    return url->u_userinfo;
  }

  llvm::StringRef getHost() const { return url->u_host; }

  llvm::StringRef getHostName() const { return url->u_hostname; }

  llvm::StringRef getPort() const { return url->u_port; }

  llvm::StringRef getPath() const { return url->u_path; }

  std::optional<llvm::StringRef> getQuery() const {
    if (!url->u_query)
      return std::nullopt;
    return url->u_query;
  }

  std::optional<llvm::StringRef> getFragment() const {
    if (!url->u_fragment)
      return std::nullopt;
    return url->u_fragment;
  }

  llvm::StringRef getReqURI() const { return url->u_requri; }
};

class AIOView {
private:
  nng_aio *aio = nullptr;

public:
  AIOView() = default;
  explicit AIOView(nng_aio *aio) : aio(aio) {}
  explicit operator bool() const { return aio; }
  nng_aio *get() noexcept { return aio; }

  void *getInput(unsigned int index) const {
    return nng_aio_get_input(aio, index);
  }

  void setOutput(unsigned int index, void *result) {
    nng_aio_set_output(aio, index, result);
  }

  void finish(int err) { nng_aio_finish(aio, err); }
};

class HTTPHandler {
private:
  std::unique_ptr<nng_http_handler, detail::HTTPHandlerDeleter> handler;

public:
  HTTPHandler() = default;
  HTTPHandler(HTTPHandler &&rhs) noexcept = default;
  HTTPHandler &operator=(HTTPHandler &&rhs) = default;
  explicit HTTPHandler(nng_http_handler *handler) noexcept : handler(handler) {}
  HTTPHandler(const HTTPHandler &rhs) = delete;
  HTTPHandler &operator=(const HTTPHandler &rhs) = delete;
  explicit operator bool() const { return static_cast<bool>(handler); }
  nng_http_handler *release() { return handler.release(); }

  static llvm::Expected<HTTPHandler> alloc(llvm::StringRef path,
                                           void (*func)(nng_aio *)) {
    nng_http_handler *result;
    int err = nng_http_handler_alloc(&result, path.str().c_str(), func);
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_handler_alloc");
    return HTTPHandler(result);
  }

  static llvm::Expected<HTTPHandler> allocRedirect(llvm::StringRef path,
                                                   uint16_t status,
                                                   llvm::StringRef location) {
    nng_http_handler *result;
    int err = nng_http_handler_alloc_redirect(&result, path.str().c_str(),
                                              status, location.str().c_str());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err,
                                         "nng_http_handler_alloc_redirect");
    return HTTPHandler(result);
  }

  static llvm::Expected<HTTPHandler> allocStatic(llvm::StringRef path,
                                                 llvm::StringRef data,
                                                 llvm::StringRef content_type) {
    nng_http_handler *result;
    int err =
        nng_http_handler_alloc_static(&result, path.str().c_str(), data.data(),
                                      data.size(), content_type.str().c_str());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_handler_alloc_static");
    return HTTPHandler(result);
  }

  llvm::Error setMethod(std::optional<llvm::StringRef> method) {
    int err = nng_http_handler_set_method(
        handler.get(), method ? method->str().c_str() : nullptr);
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_handler_set_method");
    return llvm::Error::success();
  }

  llvm::Error setTree() {
    int err = nng_http_handler_set_tree(handler.get());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_handler_set_tree");
    return llvm::Error::success();
  }
};

class HTTPServer {
private:
  std::unique_ptr<nng_http_server, detail::HTTPServerDeleter> server;

public:
  HTTPServer() = default;
  HTTPServer(HTTPServer &&rhs) noexcept = default;
  HTTPServer &operator=(HTTPServer &&rhs) = default;
  explicit HTTPServer(nng_http_server *server) noexcept : server(server) {}
  HTTPServer(const HTTPServer &rhs) = delete;
  HTTPServer &operator=(const HTTPServer &rhs) = delete;
  explicit operator bool() const { return static_cast<bool>(server); }

  static llvm::Expected<HTTPServer> hold(const URL &url) {
    nng_http_server *result;
    int err = nng_http_server_hold(&result, url.get());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_server_hold");
    return HTTPServer(result);
  }

  llvm::Error addHandler(HTTPHandler &&handler) {
    int err = nng_http_server_add_handler(server.get(), handler.release());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_server_add_handler");
    return llvm::Error::success();
  }

  llvm::Error start() {
    int err = nng_http_server_start(server.get());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_server_start");
    return llvm::Error::success();
  }

  void stop() {
    // No return code.
    nng_http_server_stop(server.get());
  }
};

class HTTPRequestView {
private:
  nng_http_req *req = nullptr;

public:
  HTTPRequestView() = default;
  HTTPRequestView(nng_http_req *req) : req(req) {}
  explicit operator bool() const { return req; }

  llvm::StringRef getData() const {
    void *body;
    size_t size;
    nng_http_req_get_data(req, &body, &size);
    return llvm::StringRef(reinterpret_cast<const char *>(body), size);
  }

  std::optional<llvm::StringRef> getHeader(llvm::Twine key) const {
    llvm::SmallVector<char, 0> buffer;
    auto key_str = key.toNullTerminatedStringRef(buffer);
    auto result = nng_http_req_get_header(req, key_str.data());
    if (!result)
      return std::nullopt;
    return result;
  }

  llvm::StringRef getMethod() const { return nng_http_req_get_method(req); }

  llvm::StringRef getURI() const { return nng_http_req_get_uri(req); }
};

class HTTPResponse {
private:
  std::unique_ptr<nng_http_res, detail::HTTPResponseDeleter> res;

public:
  HTTPResponse() = default;
  HTTPResponse(HTTPResponse &&rhs) noexcept = default;
  HTTPResponse &operator=(HTTPResponse &&rhs) = default;
  explicit HTTPResponse(nng_http_res *res) noexcept : res(res) {}
  HTTPResponse(const HTTPResponse &rhs) = delete;
  HTTPResponse &operator=(const HTTPResponse &rhs) = delete;
  explicit operator bool() const { return static_cast<bool>(res); }
  nng_http_res *release() { return res.release(); }

  static llvm::Expected<HTTPResponse> alloc() {
    nng_http_res *result;
    int err = nng_http_res_alloc(&result);
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_res_alloc");
    return HTTPResponse(result);
  }

  llvm::Error addHeader(llvm::Twine key, llvm::Twine val) {
    llvm::SmallVector<char, 64> key_buffer, val_buffer;
    auto key_str = key.toNullTerminatedStringRef(key_buffer);
    auto val_str = val.toNullTerminatedStringRef(val_buffer);
    int err =
        nng_http_res_add_header(res.get(), key_str.data(), val_str.data());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_res_add_header");
    return llvm::Error::success();
  }

  llvm::Error copyData(const llvm::Twine &body) {
    llvm::SmallVector<char, 0> buffer;
    auto body_str = body.toStringRef(buffer);
    int err =
        nng_http_res_copy_data(res.get(), body_str.data(), body_str.size());
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_res_copy_data");
    return llvm::Error::success();
  }

  llvm::Error setStatus(uint16_t status) {
    int err = nng_http_res_set_status(res.get(), status);
    if (err != 0)
      return llvm::make_error<ErrorInfo>(err, "nng_http_res_set_status");
    return llvm::Error::success();
  }
};

}; // end namespace nng
}; // end namespace memodb

#endif // MEMODB_NNG_H