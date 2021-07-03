#ifndef MEMODB_SERVER_H
#define MEMODB_SERVER_H

#include <cstdint>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <optional>

#include "Node.h"
#include "Store.h"

namespace memodb {

class Request {
public:
  virtual ~Request() {}
  virtual llvm::StringRef getMethod() const = 0;
  virtual llvm::StringRef getURI() const = 0;
  virtual std::optional<llvm::StringRef>
  getHeader(const llvm::Twine &key) const = 0;

  unsigned getAcceptQ(llvm::StringRef content_type) const;
};

class Response {
public:
  Response(const Request &request) : request(request) {}
  virtual ~Response() {}
  void sendStatus(std::uint16_t status);
  void sendHeader(const llvm::Twine &key, const llvm::Twine &value);
  void sendBody(const llvm::Twine &body);
  void sendError(std::uint16_t status, std::optional<llvm::StringRef> type,
                 llvm::StringRef title,
                 const std::optional<llvm::Twine> &detail);
  void sendNode(const Node &node, const CID &cid);

protected:
  virtual void sendStatusImpl(std::uint16_t status) = 0;
  virtual void sendHeaderImpl(const llvm::Twine &key,
                              const llvm::Twine &value) = 0;

  // If request.getMethod() == "HEAD", this should just set Content-Length
  // without sending anything.
  virtual void sendBodyImpl(const llvm::Twine &body) = 0;

  const Request &request;
  bool status_sent = false;
  bool body_sent = false;
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
