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
- :x: Text-based format is inefficient.

### HTTP/2

- ✔️ Same features as HTTP/1.
- ✔️ More efficient.
- :x: Complicated.
- :x: Few server implementations.
- :x: Many REST clients lack support.

### HTTP/3

- ✔️ Same features as HTTP/1 and HTTP/2.
- :x: Not yet standardized.
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
  - MQTT 5 adds some support for request/reply, but many client libraries don't
    support it yet.

### Minimalist request/reply (NNG, ZeroMQ, or custom)

- ✔️ Lightweight and efficient.
- :x: No standardized support for caching proxies, encryption, or
  authentication.
- :x: No standardized support for content type negotiation, compression, range
  requests, etc.

## Networking server libraries

One point of difficulty is that MemoDB needs to work even with exceptions and RTTI disabled.
That's because some official LLVM builds have them disabled, which means
in order to use templates like `llvm::cl::opt` we need to have them disabled too.
This rules out a lot of interesting libraries like Boost.Beast.

We also need the ability to respond asynchronously to requests, in case a
client requests a func evaluation and we're waiting for a distributed worker to
perform it. We also need to set per-request timeouts so can give up at the
appropriate time.

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
