# MemoDB REST API

MemoDB comes with its own REST server that can be used to access all contents
of the Store, following the [MemoDB data model]. To start the server, run:

`memodb-server --store=sqlite:/tmp/memodb.db http://127.0.0.1:7683/`

**WARNING:** The server code has not been inspected for security, and it may
well have arbitrary code execution vulnerabilities. If nothing else, it
certainly has lots of DoS vulnerabilities. Do not expose the server to the
Internet, unless you put it behind an encrypted, authenticating proxy.

## Overview

### Data formats and content negotiation

All successful requests and responses that include body data can be represented
using Nodes in the [MemoDB data model]. The server supports several different
formats for these requests and responses:

- `application/cbor`: [CBOR] binary data, supports all Nodes.
- `application/json`: [MemoDB JSON] textual data, supports all Nodes. Please
  see [MemoDB JSON] for details about this format.
- `application/octet-stream`: raw binary data, only for Nodes consisting of a
  single byte string.
- `text/html`: for responses only, a web page displaying the Node for debugging
  purposes.

Error responses are not represented using Nodes. They also support multiple
formats:

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

If the `Accept` header is missing, the server will send responses in JSON
format for maximum compatibility.

### Caching

Clients and proxies can use the standard `If-None-Match`, `ETag`, and
`Cache-Control` headers to determine which responses may be cached.

### CID formats

When the server returns a list of URIs, it will encode all CIDs in them using
the base64url Multibase. This is the preferred Multibase for all CIDs in paths,
although the server may accept other formats.

Clients that generate their own CIDs should use the exact same generation
algorithm as the server. In particular, the choice between raw data and
DAG-CBOR CIDs and the choice between identity and Blake2b-256 CIDs must match
between client and server. Otherwise, things may not work properly.

## CID endpoints

### Get a Node from its CID

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

**FIXME:** This syntax makes it impossible to handle nullary funcs (which have
no arguments). I should probably prohibit those anyway.

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

[CBOR]: https://cbor.io/
[MemoDB data model]: ./data-model.md
[MemoDB JSON]: ./json.md
[RFC 7807]: https://datatracker.ietf.org/doc/html/rfc7807
