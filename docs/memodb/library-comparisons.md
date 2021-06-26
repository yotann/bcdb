# Comparisons of libraries

## Database libraries

### SQLite

- ✔️ Widely available.
- ✔️ Widely understood.
- ✔️ Lightweight.
- ✔️ Allows simultaneous access from multiple threads.
- ✔️ Allows simultaneous access from multiple processes.
- ✔️ Efficient even for short-lived processes.
- ✔️ Supports transactions to ensure consistency.
- ❌ No compression (either for records, or for chunks containing multiple records).
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

## Networking server libraries

One point of difficulty is that MemoDB needs to work even with exceptions and RTTI disabled.
That's because some official LLVM builds have them disabled, which means
in order to use templates like `llvm::cl::opt` we need to have them disabled too.
This rules out a lot of interesting libraries like Boost.Beast.

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

### Mosquitto

Mosquitto actually implements MQTT, not HTTP. MQTT is a lightweight publish/subscribe protocol;
version 5 adds some support for request/reply.
But a lot of MQTT libraries don't support version 5 yet.

Most MemoDB traffic is request/reply, so MQTT isn't a great fit.
It also doesn't have built-in support for things like compression,
content type negotiation, or range queries, so those things would
need to be reimplemented by every client.
