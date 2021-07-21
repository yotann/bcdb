# MemoDB Protocol

**OBSOLETE:** This protocol is being replaced with the REST API (which will use
a similar design).

This protocol is used by both clients and workers to connect to a MemoDB
broker. The broker accepts jobs from clients that need to be processed, and
forwards them to workers; when a worker has finished a job and produced a
result, the broker forwards the result back to the client. This protocol has
the following goals:

- Allow jobs to be routed to workers on the basis of abstract service names.
- Allow disconnections of clients and workers to be handled gracefully.
- Allow a single broker process to serve hundreds of clients and workers
  simultaneously.
- Allow many client and worker threads to be multiplexed over a single network
  connection to the broker.
- Allow jobs and results ranging from a few dozen bytes to several megabytes to
  be handled in a reasonably efficient way.
- Allow the use of slow jobs taking several minutes or more, while still using
  shorter timeouts for faster jobs.
- Allow simple clients and workers to be implemented in a reasonably simple
  way.
- **Non-goal:** Provide authentication or encryption. This can be handled by a
  separate program such as [spiped](http://www.tarsnap.com/spiped.html).
- **Non-goal:** Provide any sort of persistence. In-progress jobs will be lost
  if the broker dies, but the clients can resubmit their jobs after the broker
  is restarted.

Each job has a service name associated with it; the job can only be sent to
workers that support that particular service. Each job also has a timeout
specified; if a worker has been processing the job for longer than the timeout,
the broker assumes that the worker has disconnected.

## Base layer

The broker listens on a [Nanomsg/NNG repv0
socket](https://nng.nanomsg.org/man/tip/nng_rep.7.html); clients and workers
use [Nanomsg/NNG reqv0 sockets](https://nng.nanomsg.org/man/tip/nng_req.7.html)
to connect to it and send requests. Each client, and each worker, can only
handle a single job at a time. But multiple clients or workers, or even a
combination of both, can be multiplexed over the same socket by using a
separate [`nng_ctx`](https://nng.nanomsg.org/man/tip/nng_ctx.5.html) for each
one.

The base protocol can be anything supported by Nanomsg/NNG, such as TCP over
IPv4 (`tcp://127.0.0.1:12703`), TCP over IPv6 (`tcp://[::1]:12701`), or IPC
(`ipc:///tmp/memodb.socket`).

## Simple client

A simple client works by sending a JOB request to the broker, and waiting for a
RESULT reply. If the broker is unable to find a worker for the job, or the
worker times out, it will drop the connection; the client should eventually
time out and retry the request. Nanomsg/NNG does this automatically if an
appropriate timeout is set.

Clients do not have unique IDs; each request is handled independently.
Therefore, clients should put an empty bytestring in the ID field of their
requests.

TODO: how should complex, asynchronous clients work? They need to coordinate
with the broker to figure out how many jobs to submit at once.

## Worker

A worker starts by sending a READY request to the broker, including the list of
services the worker supports. The broker will reply with either a JOB to be
performed by the worker, or a HEARTBEAT if no jobs are found after a certain
amount of time. The HEARTBEAT allows both the broker and the worker to verify
that they are still connected to each other.

A worker that receives a JOB should process it and then send a RESULT request
to the broker. A worker that receives a HEARTBEAT should reply immediately with
another HEARTBEAT to indicate that it's still connected. In either case, the
broker will reply with another JOB or HEARTBEAT.

If the broker detects that it is out of sync with the worker, it can send a
DISCONNECT reply to any request made by the worker. When this happens, the
worker should disconnect, reset its state, and connect again with a new READY
request. This can happen when the broker dies and restarts, and the new broker
process doesn't recognize the worker.

Each worker is assigned a unique ID by the broker, in the form of an arbitrary
bytestring. Aside from the READY message, which is sent before the worker has
been assigned an ID, every message to or from the worker should include the ID.

## Message format

Each message consists of a [CBOR](http://cbor.io/)-encoded header, followed by
an optional payload consisting of arbitrary data. The header is a CBOR array
consisting of:

1. The protocol version field, `"memo01"`.
2. A message type number, such as 0x01 for READY.
3. An ID bytestring. This should be an empty bytestring for all messages to and
   from clients, and for READY messages. For all other messages to and from
   workers, it should contain the unique worker ID assigned by the broker.
4. Possibly more fields depending on message type.

The header should be encoded with [deterministically encoded
CBOR](https://www.rfc-editor.org/rfc/rfc8949.html#name-deterministically-encoded-c).
Each message type's format is as follows:

- **READY**: `["memo01", 0x01, ID, [list of service names as strings]]`. No
  payload.
- **JOB**: `["memo01", 0x02, ID, service name as string, timeout in
  milliseconds]`. Payload format depends on the service. The timeout is the
  amount of time the broker should wait for a RESULT before giving up after
  sending the job to a worker.
- **RESULT**: `["memo01", 0x03, ID, optional bool if disconnecting]`. Payload
  format depends on the service. If the optional bool is present and true, the
  worker cannot handle any more jobs and the server should reply with
  DISCONNECT.
- **HEARTBEAT**: `["memo01", 0x04, ID]`. No payload.
- **DISCONNECT**: `["memo01", 0x05, ID]`. No payload.

## Comparison with Majordomo

This protocol is based on the [Majordomo
Protocol](https://rfc.zeromq.org/spec/7/), with several major differences:

- It's based on the Nanomsg/NNG socket library, rather than ZeroMQ. (In tests,
  ZeroMQ started dropping replies when the client made too many concurrent
  requests. And Nanomsg/NNG, unlike ZeroMQ, automatically retries requests,
  which makes simple clients and workers easier to implement.)
- Protocol fields are encoded using a CBOR header in the message, rather than a
  multi-frame message. Nanomsg/NNG doesn't support multi-frame messages.
- The client and worker use the same message format, for simplicity.
- Workers are identified by explicitly giving them an ID number, not by
  remembering which socket they connect from. This is necessary in case
  multiple workers are multiplexed over the same Nanomsg/NNG socket.
- Each worker can support multiple services, not just one.
