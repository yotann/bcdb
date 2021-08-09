# Semantic Outlining

## Performing outlining manually

These instructions assume you have built the BCDB and installed it in `PATH`.
It is recommended to enable RocksDB support.

### 0. Important notes

#### Connecting to the database or the server

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

#### Problems with the server

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

### 1. Install the newest version of BCDB

```shell
cd ~/bcdb
git pull
git submodule update --init --depth=1
nix-env -f . -iA bcdb
```

### 2. Obtain bitcode for the desired package

```shell
# Build bitcode for the "ppmtomitsu" program. This program is very small but
# it has lots of repeated code sequences for outlining.
cd ~/bcdb/nix/bitcode-overlay
nix-build -A pkgsBitcode.netpbm
bc-imitate extract result*/bin/ppmtomitsu | opt -Oz --strip-debug > ppmtomitsu.bc
```

See [nix/bitcode-overlay](../../nix/bitcode-overlay/README.md) for
more instructions. The outliner doesn't support debugging info, and it doesn't
make much sense to run it on an unoptimized program, so you should run `opt -Oz
--strip-debug` if necessary.

### 3. Initialize the MemoDB store

```sh
export MEMODB_STORE=rocksdb:$HOME/outlining.rocksdb
memodb init
bcdb add -name ppmtomitsu ppmtomitsu.bc
```

Behind the scenes, the `ppmtomitsu.bc` bitcode module is split into separate
functions, and syntactically identical functions are deduplicated.

It's okay to add multiple bitcode modules to the same store. You only need to
do this step once per module.

### 4. The main outlining process

It's okay to skip some of these steps; even if you don't explicitly run them,
they will automatically be evaluated if necessary.

Remember, all these commands need `MEMODB_STORE` set so they know which store
to access. By default they will use one thread per core, but you can use the
`-j` option to change that.

#### Identify candidates

This will identify outlining candidates in every function used in the module.
When it finishes, it prints the total number of candidates. Func names:
`smout.candidates_vN` and `smout.grouped_candidates_vN`.

```sh
smout candidates --name=ppmtomitsu
```

#### Extract callees

This will extract the outlined callee function for each candidate. Some of the
callees will be syntactically identical, so they will be deduplicated by the
BCDB (they will get the same CID). When it finishes, this command prints the
number of unique callees, which is normally smaller than the total number of
candidates because of duplicates. Func names: `smout.extracted_callees_vN`,
`smout.grouped_callees_for_function_vN`, `smout.grouped_callees_vN`.

```sh
smout extract-callees --name=ppmtomitsu
```

#### Find greedy solution (BROKEN)

This step decides which candidates should actually be outlined, avoiding
overlaps. When it finishes, it prints the chosen solution, including a
`total_benefit` item that gives the estimated code size savings for the whole
module. Func name: `smout.greedy_solution`.

```sh
smout solve-greedy --name=ppmtomitsu
```

#### Perform full outlining (BROKEN)

This step uses the greedy solution to actually perform outlining and link
everything together into one module. When it finishes, it prints the CID of the
optimized module. Func name: `smout.greedy_solution`.

```sh
smout optimize --name=ppmtomitsu

# you should copy the CID from the output of smout optimize
bcdb get /cid/uAXG... | opt --simplifycfg --function-attrs > ppmtomitsu-optimized.bc

# compile both versions to object files
llc --filetype=obj ppmtomitsu.bc -o ppmtomitsu.o
llc --filetype=obj ppmtomitsu-optimized.bc -o ppmtomitsu-optimized.o

# compare the sizes
size ppmtomitsu.o ppmtomitsu-optimized.o

# compile both versions to executables
bc-imitate clang ppmtomitsu.bc -o ppmtomitsu
bc-imitate clang ppmtomitsu-optimized.bc -o ppmtomitsu-optimized

# compare the sizes
size ppmtomitsu ppmtomitsu-optimized
```

Hopefully the optimized version has a small `.text` section. You can compare
the actual size savings from the `size` command against the estimated savings
from the `smout solve-greedy` command.

You can also run the optimized command and make sure its behavior is correct:

```sh
./ppmtomitsu </dev/null
./ppmtomitsu-optimized </dev/null
```

### Other analyses

#### Equivalence checking (BROKEN)

The equivalence checker uses a specially modified version of Alive2. The Alive2
checks are done by a separate process, so it needs to connect to
`memodb-server` in order to get jobs. First, you need to install the modified
Alive2:

```sh
cd $HOME
git clone https://github.com/yotann/alive2
cd alive2
nix-env -f . -iA alive2
```

Now you can start the server. If you're sharing a machine with other people,
you should use a random port number instead of 29179, so you don't conflict
with anyone else.

```sh
# MEMODB_STORE should be set to rocksdb:..., just like for the other commands.
memodb-server http://127.0.0.1:29179
```

Now, in a separate window, start the Alive2 workers. This command, using GNU
parallel, will run one worker per core. It will also restart the workers when
they crash (which happens pretty often, especially when the SMT solver takes
too long).

```sh
yes | parallel -u -n0 ./alive-worker http://127.0.0.1:29179
```

Finally, in a third window, start the actual equivalence checking. When this
command finishes, it should print the number of equivalent pairs. Func names:
`smout.equivalent_pairs` and `smout.equivalent_pairs_in_group`.

```sh
# Override MEMODB_STORE just for this command, so it connects to memodb-server.
MEMODB_STORE=http://127.0.0.1:29179 smout equivalence --name=ppmtomitsu
```

Remember to kill memodb-server and alive-worker when you're done with them.

#### ILP Problem (BROKEN)

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
results.
