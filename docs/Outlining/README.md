# Semantic Outlining

## Performing outlining manually

These instructions assume you have built the BCDB and installed it in `PATH`.
It is recommended to enable RocksDB support.

### 1. Obtain bitcode for the desired package

See [nix/bitcode-overlay](../../nix/bitcode-overlay/README.md) for
instructions.

### 2. Initialize the MemoDB store

```sh
export MEMODB_STORE=rocksdb:lz4.rocksdb
memodb init
bcdb add -name lz4 lz4.bc
```

Behind the scenes, the `lz4.bc` bitcode module is split into separate
functions, and syntactically identical functions are deduplicated.

### 3. Perform outlining analysis

```sh
smout extract-callees --name=lz4
```

### 4. Explore the outlining information

```sh
# In one window:
memodb-server http://127.0.0.1:51230/

# In another window:
curl http://127.0.0.1:51230/call
```

You can use the [REST API](../memodb/rest-api.md) to explore the outlining
results. The important functions are:

- `smout.candidates`: for each original function, a list of all the possible
  outlining candidates. The candidates are grouped by the expected type of the
  outlined callee function. Each candidate has the items `nodes` (indicates
  which instructions would be outlined), `fixed_overhead` (estimated number of
  bytes added if the first copy of the candidate is outlined), and
  `savings_per_copy` (estimated number of bytes saved for each additional copy,
  after the first one).
- `smout.extracted.callee`: for each candidate, the LLVM bitcode of the
  outlined callee function. If two candidates have the same result for
  `smout.extracted.callee`, they can both be outlined if only one copy of the
  callee is added.
