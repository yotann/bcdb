# Semantic Outlining

## Performing outlining manually

These instructions assume you have built the BCDB and installed it in `PATH`.
It is recommended to enable RocksDB support.

### 0. Important notes

Only one process (such as `memodb`, `memodb-server`, `bcdb`, or `smout`) can be
directly connected to the database at once (with `MEMODB_STORE=rocksdb:...`).
In particular, you have to kill `memodb-server` before you run any other
command that connects directly to the database.

It's okay to keep `memodb-server` running with direct access
(`MEMODB_STORE=rocksdb:...`) if all your other commands just connect to the
server (`MEMODB_STORE=http:...`). But keep in mind it may be slow to go through
the server like this.

#### Problems with the server

- The server and client have some known memory leaks.
- If a worker crashes while it's processing a job, the job will never be
  finished. (The server doesn't have a timeout while waiting for results.) You
  can fix this by restarting the server and then all the programs connected to
  it.

### 1. Obtain bitcode for the desired package

```shell
# Build the "ppmtomitsu" program with embedded bitcode.
# This program is very small but it has lots of repeated code sequences for
# outlining.
cd bcdb/nix/bitcode-overlay
nix-build -A pkgsBitcode.netpbm
bc-imitate extract result*/bin/ppmtomitsu > ppmtomitsu.bc
```

See [nix/bitcode-overlay](../../nix/bitcode-overlay/README.md) for
more instructions.

### 2. Initialize the MemoDB store

```sh
export MEMODB_STORE=rocksdb:outlining.rocksdb
memodb init
bcdb add -name ppmtomitsu ppmtomitsu.bc
```

Behind the scenes, the `ppmtomitsu.bc` bitcode module is split into separate
functions, and syntactically identical functions are deduplicated.

It's okay to add multiple bitcode modules to the same store.

### 3. Invalidate old cached results

BCDB caches the results of different parts of the outlining process. When you
upgrade BCDB, sometimes the old cached results will be incompatible with the
new outlining code. You can delete old cached results like this:

```sh
# List all funcs that have cached results.
memodb list-funcs
# Invalidate selected funcs
memodb invalidate smout.candidates smout.candidates_total smout.greedy_solution
```

### 4. The main outlining process

It's okay to skip some of these steps; even if you don't explicitly run them,
they will automatically be evaluated if necessary.

Remember, all these commands need `MEMODB_STORE` set so they know which store
to access. By default they will use one thread per core, but you can use the
`-j` option to change that.

#### Identify candidates

This will identify outlining candidates in every function used in the module.
When it finishes, it prints the total number of candidates. Func names:
`smout.candidates` and `smout.candidates_total`.

```sh
smout candidates --name=ppmtomitsu
```

#### Extract callees

This will extract the outlined callee function for each candidate. Some of the
callees will be syntactically identical, so they will be deduplicated by the
BCDB (they will get the same CID). When it finishes, this command prints the
number of unique callees, which is normally smaller than the total number of
candidates because of duplicates. Func names: `smout.extracted.callee` and
`smout.unique_callees`.

```sh
smout extract-callees --name=ppmtomitsu
```

#### Find greedy solution

This step decides which candidates should actually be outlined, avoiding
overlaps. When it finishes, it prints the estimated code size savings for the
whole module. Func name: `smout.greedy_solution`.

```sh
smout solve-greedy --name=ppmtomitsu
```

#### Perform full outlining

This step uses the greedy solution to actually perform outlining and link
everything together into one module. When it finishes, it prints the CID of the
optimized module. Func name: `smout.greedy_solution`.

FIXME

### Other analyses

#### Equivalence checking

The equivalence checker uses a specially modified version of Alive2. The Alive2
checks are done by a separate process, so it needs to connect to
`memodb-server` in order to get jobs. First, you need to install the modified
Alive2:

```sh
git clone https://github.com/yotann/alive2
cd alive2
nix-env -f . -iA alive2
```

Now you can start the server. If you're sharing a machine with other people,
you should use a random port number instead of 29179, so you don't conflict
with anyone else.

```sh
# The memodb-server command needs MEMODB_STORE set to access the store
# directly.
export MEMODB_STORE=rocksdb:outlining.rocksdb
memodb-server http://127.0.0.1:29179
```

Now, in a separate window, start the Alive2 workers. This command will run one
worker per core, which is recommended. It will also restart the workers when
they crash (which happens pretty often, especially when the SMT solver takes
too long).

```sh
yes | parallel -u -n0 ./alive-worker http://127.0.0.1:29179
```

Finally, in a third window, start the actual equivalence checking. When this
command finishes, it should print the number of equivalent pairs. Func names
`smout.equivalent_pairs` and `smout.equivalent_pairs_in_group`.

```sh
MEMODB_STORE=http://127.0.0.1:29179 smout equivalence --name=ppmtomitsu
```

Remember to kill memodb-server and alive-worker when you're done with them.

#### ILP Problem

TODO: explain.

#### Explore the cached outlining results

You need to run the other steps first, in order to fill the database with
results.

```sh
# In one window:
memodb-server http://127.0.0.1:29179

# In another window:
curl http://127.0.0.1:29179/call
```

You can use the [REST API](../memodb/rest-api.md) to explore the outlining
results. The important functions are:

- `smout.candidates`: for each original function, a list of all the possible
  outlining candidates. The candidates are grouped by the expected type of the
  outlined callee function. Each candidate has these items:
  - `nodes` indicates which ranges of instructions would be outlined. For
    example, `[0, 1, 9, 12]` means that instructions 0, 9, 10, and 11 would be
    outlined.
  - `callee_size` is the estimated number of bytes added in order to create the
    outlined callee function.
  - `caller_savings` is the estimated number of bytes saved in the outlined
    caller function by performing outlining.
- `smout.extracted.callee`: for each candidate, the LLVM bitcode of the
  outlined callee function. If two candidates have the same result for
  `smout.extracted.callee`, they can both be outlined sharing just one copy of
  the callee.
