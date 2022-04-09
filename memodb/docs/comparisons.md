# Comparisons of protocols and libraries

## Database libraries

### SQLite

- ✔️ Widely available.
- ✔️ Widely understood.
- ✔️ Lightweight.
- ✔️ Allows simultaneous access from multiple threads.
- ✔️ Allows simultaneous access from multiple processes.
- ✔️ Efficient even for short-lived processes.
- ✔️ Supports transactions to ensure consistency.
- :x: No compression (either for records, or for chunks containing multiple records).
- :x: Not as fast as RocksDB.
- :x: Have to periodically flush the write-ahead log, which may cause stalls.

### RocksDB

- ✔️ Very fast.
- ✔️ Reasonably lightweight.
- ✔️ Can compress multiple records together using Zstandard.
- ✔️ Key-value semantics are easy to understand.
- ✔️ Allows simultaneous access from multiple threads.
- :x: Does not allow simultaneous access from multiple processes.
- :x: Not efficient for short-lived processes.
- :x: Somewhat complicated to tune (apparently Facebook uses machine learning internally to tune RocksDB).
- :x: Not widely available.

## Networking protocols

### HTTP/1

- ✔️ Ubiquitous.
- ✔️ Proxies can add caching, authentication, encryption, etc.
- ✔️ Content type negotiation.
- ✔️ Optional compression.
- ✔️ Optional encryption.
- ✔️ Optional authentication.
- ✔️ Range requests.
- :x: No push support.
- :x: Only one request per connection.
- :x: Text-based format is inefficient.

### HTTP/2

- ✔️ Same features as HTTP/1.
- ✔️ More efficient.
- ✔️ Supports simultaneous requests over one connection.
- :x: Susceptible to TCP head-of-line blocking.
- :x: Complicated.
- :x: Few server implementations.
- :x: Many REST clients lack support.

### HTTP/3

- ✔️ Same features as HTTP/1 and HTTP/2.
- ✔️ Supports simultaneous requests over one connection.
- :x: Not yet standardized.
- :x: Complicated.
- :x: Encryption is mandatory.
- :x: Few server implementations.
- :x: Many REST clients lack support.

### CoAP

[Constrained Application Protocol](https://coap.technology/). The MemoDB REST
API is designed to use a CoAP-compatible subset of HTTP, in case we ever want
to implement it in the future. (For instance, redirects are never used.)

- ✔️ Lightweight and efficient.
- ✔️ Removes unnecessary features from HTTP.
- ✔️ Proxies can add caching, authentication, encryption, etc.
- ✔️ Content type negotiation.
- ✔️ Optional compression.
- ✔️ Optional encryption.
- ✔️ Optional authentication.
- ✔️ Range requests.
- ✔️ Supports UDP, TCP, and WebSockets.
- ✔️ Supports many simultaneous requests over one connection.
- ✔️ Supports publish/subscribe via the
  [OBSERVE](https://datatracker.ietf.org/doc/html/rfc7641) extension.
- :x: Not widely supported.

### MQTT

- ✔️ Lightweight and efficient.
- ✔️ Fairly widely supported.
- ✔️ Optional encryption.
- ✔️ Optional authentication.
- :x: No standardized support for content type negotiation, compression, range
  requests, etc.
- :x: Mainly designed for publish/subscribe, but MemoDB is mainly
  request/reply.
  - Then again, maybe it's easier to implement request/reply on top of
    publish/subscribe than the other way around.
  - MQTT 5 adds some support for request/reply, but many client libraries don't
    support it yet. Older versions don't have a good way to say "publish this
    to one subscriber only", which is needed to submit jobs without a broker.
  - A worker will have to constantly subscribe and unsubscribe, to make sure
    it's only subscribed while it has threads available to do work.

### Minimalist request/reply (NNG, ZeroMQ, or custom)

- ✔️ Lightweight and efficient.
- :x: No standardized support for caching proxies, encryption, or
  authentication.
- :x: No standardized support for content type negotiation, compression, range
  requests, etc.

## Networking server libraries

One point of difficulty is that official LLVM packages are built with
exceptions disabled. In order to use templates like `llvm::cl::opt`, either we
need to have exceptions disabled too, or we need users to build a custom
version of LLVM with exceptions enabled.

### Boost.Beast + Boost.Asio

- ✔️ Widely available.
- ✔️ Well documented.
- ✔️ Supports HTTP/1 and WebSockets.
- ✔️ Supports TCP, Unix sockets, in-process communication, etc.
- ✔️ Includes general asynchronous I/O features.
- ✔️ Should work well with Boost.Fiber etc.
- ✔️ Optional support for TLS (using OpenSSL).
- :x: Requires RTTI and exceptions (maybe Boost.Beast could be patched to use
  Boost.ThrowException?)

### NNG

- ✔️ Decently well documented.
- ✔️ Supports HTTP/1, WebSockets, and minimalist request/reply protocols.
- ✔️ Supports TCP, Unix sockets, in-process communication, etc.
- ✔️ Includes general asynchronous I/O features including custom timers.
- ✔️ Acceptably lightweight.
- ✔️ Doesn't need RTTI or exceptions (nngpp does, but it's optional).
- ✔️ Optional support for TLS (using mbedTLS).
- :x: No support for HTTP/2.
- :x: No built-in support for HTTP features like compression.
- :x: May be difficult to avoid deadlock.
- :x: Not widely available.

### libh2o

- ✔️ Supports HTTP/1, HTTP/2, HTTP/3, and WebSockets.
- ✔️ Can be built on libuv, which includes general asynchronous I/O features.
- ✔️ Acceptably lightweight.
- ✔️ Optional support for TLS (using OpenSSL).
- ✔️ Doesn't need RTTI or exceptions.
- :x: Not especially well documented.
- :x: No support for minimalist request/reply protocols.
- :x: Not widely available.

### uWebSockets

- ✔️ Supports HTTP/1 and WebSockets.
- ✔️ Lightweight.
- ✔️ Doesn't need RTTI or exceptions.
- :x: No support for Unix sockets.
- :x: No support for HTTP/2 or minimalist request/reply protocols.
- :x: Not widely available.
