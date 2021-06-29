#ifndef MEMODB_NNG_H
#define MEMODB_NNG_H

// This is a C++ wrapper for the NNG library, like nngpp, except that it
// doesn't use exceptions. This is important if the LLVM build we're using had
// exceptions disabled, as some of the official builds do.

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <memory>
#include <nng/nng.h>
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

namespace detail {
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

}; // end namespace nng
}; // end namespace memodb

#endif // MEMODB_NNGMM_H
