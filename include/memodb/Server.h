#ifndef MEMODB_SERVER_H
#define MEMODB_SERVER_H

#include <cstdint>
#include <optional>

#include "Store.h"

namespace memodb {

class Request {
public:
  virtual ~Request() {}
  virtual llvm::StringRef getMethod() const = 0;
  virtual llvm::StringRef getURI() const = 0;
  virtual std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const = 0;
};

class Response {
public:
  virtual ~Response() {}
  virtual void sendStatus(std::uint16_t status) = 0;
  virtual void sendHeader(const llvm::Twine &key, const llvm::Twine &value) = 0;
  virtual void sendBody(const llvm::Twine &body) = 0;
};

class Server {
public:
  Server(Store &store);
  void handleRequest(const Request &request, Response &response);

private:
  Store &store;
};

} // end namespace memodb

#endif // MEMODB_SERVER_H
