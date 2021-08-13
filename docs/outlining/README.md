# Semantic Outlining

## Performing outlining manually

These instructions assume you have built the BCDB and installed it in `PATH`.
It is recommended to enable RocksDB support.

These instruction also assume you have gone through the [MemoDB tutorial].

### 1. Install the newest version of BCDB

```shell
cd ~/bcdb
git pull
git submodule update --init --depth=1
nix-env -f . -iA bcdb
```

If you install BCDB manually instead, make sure to compile with optimizations
enabled (`cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`).

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

#### Find greedy solution

This step decides which candidates should actually be outlined, avoiding
overlaps. When it finishes, it prints the chosen solution, including a
`total_benefit` item that gives the estimated code size savings for the whole
module. Func name: `smout.greedy_solution_vN`.

```sh
smout solve-greedy --name=ppmtomitsu
```

#### Perform full outlining

This step uses the greedy solution to actually perform outlining and link
everything together into one module. When it finishes, it prints the CID of the
optimized module. Func name: `smout.optimized_vN`.

```sh
smout optimize --name=ppmtomitsu

# you should copy the CID from the output of smout optimize
# opt is important to clean up and optimize the outlined code
bcdb get /cid/uAXG... | opt --simplifycfg --function-attrs > ppmtomitsu-optimized.bc

# compile both versions to object files
llc --filetype=obj ppmtomitsu.bc -o ppmtomitsu.o
llc --filetype=obj ppmtomitsu-optimized.bc -o ppmtomitsu-optimized.o

# compare the sizes
size ppmtomitsu.o ppmtomitsu-optimized.o

# compile both versions to executables
bc-imitate clang -Oz ppmtomitsu.bc -o ppmtomitsu
bc-imitate clang -Oz ppmtomitsu-optimized.bc -o ppmtomitsu-optimized

# compare the sizes
size ppmtomitsu ppmtomitsu-optimized
```

Hopefully the optimized version has a smaller `.text` section. You can compare
the actual size savings from the `size` command against the estimated savings
from the `smout solve-greedy` command.

You can also run the optimized command and make sure its behavior is correct:

```sh
./ppmtomitsu </dev/null
./ppmtomitsu-optimized </dev/null
```

### Other analyses

#### Equivalence checking

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

If you install Alive2 manually instead, make sure to compile with optimizations
enabled (`cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`).

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
`alive.tv`, `smout.refinements_for_group_vN`, and
`smout.grouped_refinements_vN`.

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

[MemoDB tutorial]: ../memodb/tutorial.md
