# MemoDB REST API

MemoDB comes with its own REST server that can be used to access all contents
of the Store, following the [MemoDB data model]. In the future, the REST server
will also support clients to submit tasks, and distribute those tasks to
distributed workers for processing.

To start the server, run:

`memodb-server --store=sqlite:/tmp/memodb.db http://127.0.0.1:7683/`

**WARNING:** The server code has not been inspected for security, and it may
well have severe security vulnerabilities. If nothing else, it certainly has
lots of DoS vulnerabilities. Do not expose the server to the Internet, unless
you put it behind an encrypting, authenticating proxy.

## Problems with the server

- The server probably has security holes, so you should be careful not to
  expose it to the Internet. (Running it on `http://127.0.0.1:29179` should be
  safe, but `http://0.0.0.0:29179` is dangerous.)
- If you're using the same machine as someone else, you need to use different
  port numbers (you can't both use port 29179 at the same time).
- If you kill a worker while it's processing a job, the job will never be
  finished. (The server will keep waiting for results forever.) You can fix
  this by restarting the server. You'll also have to restart all the programs
  connected to it.
  - Note that `alive-worker` is designed to send a result to the server even
    when it crashes.

## Overview

### Data formats and content negotiation

For successful requests and responses that include body data, the body data can
always be represented using Nodes in the [MemoDB data model]. The server
supports several different formats for these requests and responses:

- `application/cbor`: [CBOR] binary data, supports all Nodes.
- `application/json`: [MemoDB JSON] textual data, supports all Nodes. Please
  see [MemoDB JSON] for details about this format.
- `application/octet-stream`: raw binary data, only for Nodes consisting of a
  single byte string.
- `text/html`: for responses only, a web page displaying the Node for debugging
  purposes.

Unlike successful responses, error responses are not represented using Nodes.
They support multiple formats of their own:

- `application/problem+json`: a problem detail in the format specified by
  [RFC 7807].
- `text/html`: a web page displaying the error message for debugging purposes.

For request data, clients should indicate the format using `Content-Type` as
usual. Clients should indicate which response formats they prefer using the
standard `Accept` header. For example, a client that prefers raw binary data
but also accepts CBOR could make a request like this:

```http
GET /cid/uAXEABYIYfBiF HTTP/1.1
Accept: application/octet-stream, application/cbor;q=0.9, application/problem+json
```

If the `Accept` header does not specify a preference, the server will send
responses in JSON format for maximum compatibility.

### Caching

Clients and proxies can use the standard `If-None-Match`, `ETag`, and
`Cache-Control` headers to determine which responses may be cached.

### CID formats

When the server returns a list of URIs, it will encode all CIDs in them using
the base64url Multibase. This is the preferred Multibase for all CIDs in paths,
although the server may accept other formats.

Clients that generate their own CIDs should use the exact same generation
algorithm as the server. In particular, the choice between raw data, DAG-CBOR,
and DAG-CBOR-Unrestricted CIDs and the choice between identity and Blake2b-256
CIDs must match between client and server. Otherwise, things may not work
properly.

### Head formats

Forward slashes in head names may optionally be escaped. For example
`/head/a/b` and `/head/a%2Fb` are equivalent. When the server returns a list of
head names, it will not escape slashes.

Note that other characters (such as backslashes) must still be escaped if
required by the URI specification.

## CID endpoints

### Get a Node by looking up its CID

```http
GET /cid/:cid HTTP/1.1

200 OK
Content-Type: application/json
...
```

Example command: `curl http://127.0.0.1:7683/cid/uAXEABYIYfBiF`.

### Insert a Node

```http
POST /cid HTTP/1.1
Content-Type: application/json
...

201 Created
Location: /cid/:cid
```

## Head endpoints

### Get a list of all head URIs

```http
GET /head HTTP/1.1

200 OK
Content-Type: application/json

["/head/a","/head/b",...]
```

### Get the CID that a head is associated with

```http
GET /head/:name HTTP/1.1

200 OK
Content-Type: application/json

{"cid":...}
```

### Create or reassign a head

```http
PUT /head/:name HTTP/1.1
Content-Type: application/json

{"cid":...}

201 Created
```

## Call endpoints

The full path for a call looks like this: `/call/add/uAXEAAQI,uAXEAAQI`. The
arguments are comma-separated.

### Get a list of all func URIs

```http
GET /call HTTP/1.1

200 OK
Content-Type: application/json

["/call/a","/call/b",...]
```

### Get a list of all cached calls of a given func

```http
GET /call/:func HTTP/1.1

200 OK
Content-Type: application/json

["/call/:func/cid0,cid1","/call/:func/cid2,cid3",...]
```

### Delete all cached calls of a given func

This is useful when the func has been upgraded and needs to be recalculated, or
when the cached results are no longer necessary.

```http
DELETE /call/:func HTTP/1.1

204 No Content
```

### Get the cached value of a call

```http
GET /call/:func/:cid0,:cid1,:cid2,... HTTP/1.1

200 OK
Content-Type: application/json

{"cid":...}
```

### Add a new result to the cache

```http
PUT /call/:func/:cid0,:cid1,:cid2,... HTTP/1.1
Content-Type: application/json

{"cid":...}

201 Created
```

### Start evaluation of a call

```http
POST /call/:func/:cid0,:cid1,:cid2,.../evaluate HTTP/1.1

202 Accepted
```

The server will return `202 Accepted` at first, and add the call to a queue. A
worker process connected to the server, such as `alive-worker` or `smout
worker`, can retrieve the job, evaluate it, and submit the results to the
server using `PUT`. After results are available, the next time you `POST` the
call, the server will respond with `200 OK` and the result of the job.

It is recommended to send the `POST` request once a second until you get `200
OK` back. If you have 1000s of jobs, you can submit each of them with one
`POST` request, and then wait for the results of each job, one at a time, with
more `POST` requests.

[CBOR]: https://cbor.io/
[MemoDB data model]: ./data-model.md
[MemoDB JSON]: ./json.md
[RFC 7807]: https://datatracker.ietf.org/doc/html/rfc7807
