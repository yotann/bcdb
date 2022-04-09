# MemoDB Tutorial

This tutorial will show you how to use the `memodb` program to interact with a
MemoDB store. It's also useful to understand how MemoDB works in general.

## Building MemoDB

See the [main README.md] for build instructions.

## Creating a store

A MemoDB store is basically a database where all the data is stored. You first
need to choose which type of database to use. We'll use SQLite here, although
RocksDB is better for huge amounts of data. Then you need to set `MEMODB_STORE`
to the location of the database. Finally, run `memodb init` to create the
database for the first time.

```console
$ export MEMODB_STORE=sqlite:$HOME/memodb-tutorial.db
$ memodb init
$ ls $HOME/memodb-tutorial.db
/home/user/memodb-tutorial.db
```

**NOTE:** You must always have `MEMODB_STORE` set before you run any `memodb`,
`bcdb`, or `smout` commands. Otherwise you'll just get an error message.

## Nodes and CIDs

### Adding a Node

A Node is the basic data item stored in MemoDB. Let's add some simple Nodes to
the MemoDB store, then retrieve them.

**NOTE:** Unfortunately, the word "node" is overloaded. Don't confuse MemoDB
Nodes with outlining dependence nodes or other kinds of nodes.

```console
$ echo '27' | memodb add
/cid/uAXEAAhgb
$ memodb get /cid/uAXEAAhgb
27
$ echo '-99' | memodb add
/cid/uAXEAAjhi
$ memodb get /cid/uAXEAAjhi
-99
```

### CIDs and Deduplication

Whenever you add a Node to the MemoDB store, you get a CID back, like
`uAXEAAhgb` or `uAXEAHHgaYWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo`. A CID is a
unique identifier for a particular Node, usually based on a cryptographic hash
of the Node's data. In order to get the original Node back out of the store,
you need to use its CID.

A given Node value will always have the same CID, no matter how many times you
add it, and even if you add it to multiple different MemoDB stores.

```console
$ echo '27' | memodb add
/cid/uAXEAAhgb
$ echo '27' | memodb add
/cid/uAXEAAhgb
$ echo '27' | memodb add
/cid/uAXEAAhgb
```

In fact, when you add the same Node multiple times to a single MemoDB store,
only one copy will actually be stored. MemoDB calculates the CID of the Node,
detects that a Node with the same CID is already present in the store, and
reuses the existing Node. So no matter how many times you add the same Node,
the database won't get any larger.

### Kinds of Node

Aside from integers, there are many other kinds of Node. See [MemoDB data
model] for more details.

```console
$ # null
$ echo 'null' | memodb add
/cid/uAXEAAfY
$ # booleans
$ echo 'true' | memodb add
/cid/uAXEAAfU
$ # integers (64-bit)
$ echo '128758927483' | memodb add
/cid/uAXEACRsAAAAd-qFQew
$ # floating-point values (64-bit)
$ echo '{"float":"6.283"}' | memodb add
/cid/uAXEACftAGSHKwIMSbw
$ # text strings (valid Unicode)
$ echo '"∞"' | memodb add
/cid/uAXEABGPiiJ4
$ # byte strings
$ echo '{"base64":"Vao="}' | memodb add
/cid/uAVUAAlWq
$ # lists
$ echo '[1,2,3]' | memodb add
/cid/uAXEABIMBAgM
$ # maps
$ echo '{"map":{"foo":"bar","oof":"rab"}}' | memodb add
/cid/uAXEAEaJjZm9vY2JhcmNvb2ZjcmFi
$ # links to other Nodes
$ echo '{"cid":"uAXEAAhgb"}' | memodb add
/cid/uAXEACtgqRwABcQACGBs
```

Note that each one of these values has a different CID.

### JSON and CBOR

So far, we've been using JSON format for Nodes, because it's easy to read. But
the native format of MemoDB is actually [CBOR], not JSON. CBOR is a binary
format and it's much more efficient than JSON. There are lots of [CBOR
implementations] for different programming languages.

```console
$ memodb get --format=cbor /cid/uAXEAAhgb | hexdump -C
00000000  18 1b                                             |..|
00000002
$ memodb get --format=cbor /cid/uAXEAEaJjZm9vY2JhcmNvb2ZjcmFi | hexdump -C
00000000  a2 63 66 6f 6f 63 62 61  72 63 6f 6f 66 63 72 61  |.cfoocbarcoofcra|
00000010  62                                                |b|
00000011
```

If you go the the [CBOR playground], enter `18 1b` under "Bytes", and press the
left arrow, it will decode the CBOR for you: `27`. If you do the same thing
with `a2 63 66 6f 6f 63 62 61 72 63 6f 6f 66 63 72 61 62`, it will decode the
map: `{"foo": "bar", "oof": "rab"}`.

You might notice that the JSON version of the map is wrapped in `{"map":...}`,
but the CBOR version isn't. CBOR supports more kinds of value than JSON does,
so when we convert values to JSON, we have to add some extra wrappers ("special
objects") to distinguish between different kinds of value. This affects maps,
links, byte strings, and floating-point values. See [MemoDB JSON] for more
details.

Whenever you work with JSON, you need to remember to use these special objects.
It might be easier to work with CBOR instead, which doesn't need special
objects (although CIDs encoded in CBOR can be a bit tricky).

```console
$ # incorrect: adding a map encoded in JSON without a special object
$ echo '{"foo":"bar","oof":"rab"}' | bin/memodb add
value read: Invalid MemoDB JSON: Invalid special JSON object
$ # correct: adding a map encoded in JSON with a special object
$ echo '{"map":{"foo":"bar","oof":"rab"}}' | memodb add
/cid/uAXEAEaJjZm9vY2JhcmNvb2ZjcmFi
$ # correct: adding a map encoded in CBOR without a special object
$ echo 'A263666F6F63626172636F6F6663726162' | basenc -d --base16 | bin/memodb add --format=cbor
/cid/uAXEAEaJjZm9vY2JhcmNvb2ZjcmFi
```

### Nodes that link to other Nodes

Nodes can use CIDs to refer to other Nodes that are already in the store.
MemoDB even keeps track of which Nodes refer to which other ones.

```console
$ echo '{"cid":"uAXEAAhgb"}' | memodb add
/cid/uAXEACtgqRwABcQACGBs
$ echo '[{"cid":"uAXEAAhgb"},{"cid":"uAXEACRsAAAAd-qFQew"}]' | memodb add
/cid/uAXEAHILYKkcAAXEAAhgb2CpOAAFxAAkbAAAAHfqhUHs
$ memodb refs-to /cid/uAXEAAhgb
/cid/uAXEACtgqRwABcQACGBs
/cid/uAXEAHILYKkcAAXEAAhgb2CpOAAFxAAkbAAAAHfqhUHs
$ memodb get --format=json /cid/uAXEACtgqRwABcQACGBs
{"cid":"uAXEAAhgb"}
```

**NOTE:** The `sqlite:` store keeps track of all references, but the `rocksdb:`
store only keeps track of some of them.

There are several reasons you might want to use links:

- Break a huge Node into smaller pieces, which are easier to work with.
- Apply funcs (see below) to each piece of a value separately.
- Deduplicate different parts of a value. If each part is a separate Node, and
  two parts are identical, they will be automatically deduplicated by MemoDB.

## Bitcode example

When you use `bcdb add` to add a bitcode module to the store, each function
definition gets stored as a separate Node, so identical functions will be
deduplicated. Let's try an example.

**NOTE:** You may get different CIDs, depending on your version of LLVM.

```console
$ bcdb add --name=two_funcs - <<EOF
define void @f0() {
  ret void
}
define void @f1() {
  ret void
}
EOF
$ memodb get /head/two_funcs
/cid/uAXGg5AIgCE1k0u-eS2S8M56EMUXNGbCrl34N0RTU4pi3fKZrzq8
$ memodb get /cid/uAXGg5AIgCE1k0u-eS2S8M56EMUXNGbCrl34N0RTU4pi3fKZrzq8
{"map":{"functions":{"map":{"f0":{"cid":"uAVWg5AIgwhkXXCJpEYLJhS3PiwvQziXnVX-hwDtW1G3QXaVzvV4"},"f1":{"cid":"uAVWg5AIgwhkXXCJpEYLJhS3PiwvQziXnVX-hwDtW1G3QXaVzvV4"}}},"remainder":{"cid":"uAVWg5AIg8kkAzu3VthlmGwDrygSZqy2K0Zgqfi3D1pawvXKGp1I"}}}
$ memodb get /cid/uAVWg5AIgwhkXXCJpEYLJhS3PiwvQziXnVX-hwDtW1G3QXaVzvV4
{"base64":"QkPA3jVgAAAHAAAAA..."}
```

Both functions are the same, so they have the same CID, and only one copy is
actually stored in the database.

## Heads

You may have noticed the `/head/` path in the last section. Heads allow you to
assign a convenient name to a particular CID, which would otherwise be hard to
remember. The `bcdb add --name=two_funcs` command automatically updated the
head `two_funcs` to point to the new Node it created.

```console
$ memodb set /head/twentyseven /cid/uAXEAAhgb
$ memodb set /head/random_number /cid/uAXEAAhgb
$ memodb get /head/twentyseven
/cid/uAXEAAhgb
$ memodb get /head/random_number
/cid/uAXEAAhgb
$ memodb set /head/random_number /cid/uAXEAAjhi
$ memodb get /head/random_number
/cid/uAXEAAjhi
$ memodb get /head
/head/random_number
/head/twentyseven
/head/two_funcs
```

## Funcs and calls

Aside from Nodes and heads, call results are the last way of storing data in
MemoDB. When you have a func such as `test.add` that takes one or more Nodes as
arguments, you can evaluate the func on some Nodes:

```console
$ echo '2' | memodb add
/cid/uAXEAAQI
$ echo '3' | memodb add
/cid/uAXEAAQM
$ memodb evaluate /call/test.add/uAXEAAQI,uAXEAAQM
uAXEAAQU
$ memodb evaluate /call/test.add/uAXEAAQM,uAXEAAQI
uAXEAAQU
$ memodb get /cid/uAXEAAQU
5
```

**NOTE:** Don't get the terminology confused! A MemoDB func is different from a
C++ function or an LLVM IR function. MemoDB funcs are usually *implemented*
using C++ functions, but they have to be specially designed C++ functions and
they must be registered with `Evaluator::registerFunc()`.

**NOTE:** Different funcs may be available depending on which program you're
running. For example, `memodb evaluate /call/smout.candidates...` won't work,
but `smout evaluate /call/smout.candidates...` will. If you're using
`memodb-server`, the available funcs depend on which worker programs are
connected to the server.

### Caching

Whenever you evaluate a func, the result is automatically cached in the MemoDB
store. The next time you evaluate it using the same arguments, MemoDB will use
the cached result instead of actually evaluating the func again. This is
extremely useful for funcs that take a long time to evaluate!

```console
$ memodb get /call
/call/test.add
$ memodb get /call/test.add
/call/test.add/uAXEAAQI,uAXEAAQM
/call/test.add/uAXEAAQM,uAXEAAQI
$ # same result, but it comes from the cache this time
$ memodb evaluate /call/test.add/uAXEAAQI,uAXEAAQM
uAXEAAQU
$ # memodb get won't evaluate new calls, but it will return cached results if any
$ memodb get /call/test.add/uAXEAAQI,uAXEAAQM
/cid/uAXEAAQU
$ memodb get /call/test.add/uAXEAAQI,uAXEAAQI
error: Not Found
Call not found in store.
$ memodb evaluate /call/test.add/uAXEAAQI,uAXEAAQI
uAXEAAQQ
$ memodb get /call/test.add/uAXEAAQI,uAXEAAQI
/cid/uAXEAAQQ
```

This type of caching is also known as [memoization]. In fact, the name "MemoDB"
stands for "Memoizing Database".

### Invalidation

When you update BCDB or change the code for a func, sometimes the old cached
results are no longer valid and you want to stop using them. You want to get
rid of the old cached results and make sure all calls are evaluated again using
the new code. One way of doing this is to manually invalidate all the old
results. For example:

```console
$ memodb get /call/test.add/uAXEAAQI,uAXEAAQI
/cid/uAXEAAQQ
$ memodb delete /call/test.add
deleted
$ memodb get /call/test.add/uAXEAAQI,uAXEAAQI
error: Not Found
Call not found in store.
$ # this will reevaluate the test.add call, since the old result was invalidated
$ memodb evaluate /call/test.add/uAXEAAQI,uAXEAAQI
uAXEAAQQ
```

### Versioning

Instead of manually invalidating the calls, another option is to change the
name of the func whenever you change the code. For example, if you change the
code in `smout::optimized`, you would also change the name of the func from
`smout.optimized_v4` to `smout.optimized_v5`. The old cached results will be
ignored because the func name doesn't match, and this will work for everyone
who uses the code even if they don't invalidate the old results.

### Funcs that evaluate other funcs

Some funcs will automatically evaluate other funcs. For example, when you
evaluate `smout.grouped_candidates` on an LLVM module, it automatically
evaluates `smout.candidates` on each function in the module. Each of these
evaluations is cached as you would expect.

When you change the code for a func, you have to remember to invalidate or
change the name of *all* funcs that depend on the changed code, even
indirectly. For example, if you invalidate `smout.candidates`, you should also
invalidate `smout.grouped_candidates`, `smout.grouped_callees`,
`smout.greedy_solution`, and most of the other smout funcs, because they all
indirectly depend on the result of `smout.candidates`.

## MemoDB server

**WARNING:** The MemoDB server has some known problems and memory leaks, and
it's slower than using a direct connection to the database. For now, you should
avoid using it unless you really need it (e.g., to use `alive-worker`).

Aside from directly connecting to the database with `memodb`, you can also run
the `memodb-server` command, which connects to the database and starts an HTTP
server with a REST API. You can write your own code in other programming
languages, like Python, to connect to the server and access the MemoDB store
without having to start a new `memodb` command each time. You can also use the
server for distributed computing, if you start worker programs on multiple
different computers and connect them to the server.

To start the server:

```console
$ # the server should be started with MEMODB_STORE set to an sqlite:
$ # or rocksdb: database, just like the other commands in the tutorial.
$ export MEMODB_STORE=sqlite:$HOME/memodb-tutorial.db
$ memodb-server http://127.0.0.1:29179
Server started!
```

Now, in a different terminal, you can try connecting to the server to access
the MemoDB store.

```console
$ curl http://127.0.0.1:29179/head
["/head/two_funcs","/head/twentyseven","/head/random_number"]
$ curl http://127.0.0.1:29179/twentyseven
{"cid":"uAXEAAhgb"}
$ MEMODB_STORE=http://127.0.0.1:29179 memodb get /cid/uAXEAAhgb
27
```

See the [REST API documentation] for more details.

### Problems with the server

- The server probably has security holes, so you should be careful not to
  expose it to the Internet. (Running it on `http://127.0.0.1:29179` should be
  safe, but `http://0.0.0.0:29179` is dangerous.)
- If you're using the same machine as someone else, you need to use different
  port numbers (you can't both use port 29179 at the same time).
- The server and client have some known memory leaks.
- If you kill a worker while it's processing a job, the job will never be
  finished. (The server will keep waiting for results forever.) You can fix
  this by restarting the server. You'll also have to restart all the programs
  connected to it.
  - Note that `alive-worker` is designed to send a result to the server even
    when it crashes.

### Direct and indirect connections

The commands `memodb`, `bcdb`, and `smout` can be run in two modes: they can
either connect directly to the database store, with `MEMODB_STORE=rocksdb:...`,
or they can connect to a running instance of `memodb-server`, with
`MEMODB_STORE=http:...`. The `memodb-server` program itself should always be
run with a direct connection.

Only one process can be directly connected to the database at once. In
particular, you have to kill `memodb-server` before you run any other command
that connects directly to the database.

It's okay to run multiple programs plus `memodb-server` at the same time, as
long as `memodb-server` is the only program with a direct connection to the
database. But keep in mind it may be slow to go through the server like this.

[CBOR]: http://cbor.io/
[CBOR implementations]: http://cbor.io/impls.html
[CBOR playground]: http://cbor.me/
[main README.md]: ../../README.md
[MemoDB data model]: ./data-model.md
[MemoDB JSON]: ./json.md
[memoization]: https://en.wikipedia.org/wiki/Memoization
[REST API documentation]: ./rest-api.md
