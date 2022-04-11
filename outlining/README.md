# Semantic Outlining

**WORK IN PROGRESS:** this code is still under development and is not ready for
use yet.

## Performing outlining manually

These instructions assume you have built the BCDB and installed it in `PATH`.
It is recommended to enable RocksDB support.

These instruction also assume you have gone through the [MemoDB tutorial].

### 1. Install the newest version of BCDB

```shell
cd ~/bcdb
git pull
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

See [nix/bitcode-overlay](../nix/bitcode-overlay/README.md) for more
instructions. The outliner doesn't support debugging info, and it doesn't make
much sense to run it on an unoptimized program, so you should run `opt -Oz
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

### Outlining options

The following options affect all `smout` subcommands, because they affect
candidate generation:

- `--max-args=10`: maximum combined number of arguments and return values for
  an outlined callee. Increase to generate more candidates, but candidates with
  more arguments are less likely to be profitable to outline.
- `--max-nodes=50`: for the dependency-based candidate generator, maximum
  number of nodes in a single candidate.
- `--max-adjacent=10`: for the adjacent-node candidate generator, maximum
  number of nodes in a single candidate.
- `--min-rough-caller-savings=1`: minimum estimated benefit in the
  caller for a candidate to be generated in the first place. Increase this to
  make `smout` faster by reducing the number of candidates (but the results may
  be worse). If you use `--compile-all-callers`, you can decrease this option
  to a negative number in order to generate candidates that are estimated to be
  unprofitable, just in case it turns out they actually **are** profitable
  after being compiled.

The following options only affect `smout solve-greedy` and `smout optimize`,
because they affect which candidates are selected for outlining.

- `--min-caller-savings=1`: minimum size savings in the caller, per copy of the
  candidate, to consider a candidate for outlining. This is always based on the
  estimated size.
- `--min-benefit=1`: minimum benefit, across all duplicates, to consider a
  candidate for outlining. This is usually based on the estimated size, unless
  `--compile-all-callers` is given, in which case it is based on the actual
  compiled size.
- `--verify-caller-savings`: before outlining a candidate, compile the modified
  callers to make sure the candidate actually makes them smaller. Candidates
  are still chosen based on the estimated size savings, but candidates that
  turn out to be unprofitable will be skipped.
- `--compile-all-callers`: exhaustively compile the modified callers for all
  candidates being considered. Candidates will be chosen based on the actual
  compiled size, not the estimated size. If you use this option, there's no
  reason to use `--verify-caller-savings`.

If you don't use `--verify-caller-savings` or `--compile-all-callers`, it's
usually best to use the options `--min-caller-savings=16 --min-benefit=128` or
similar values. If you use `--verify-caller-savings` or
`--compile-all-callers`, the other options can use lower values.

#### Using options consistently for different commands

If you want to use the results from multiple different commands, like `smout
candidates` and `smout optimize`, you should use the same options for both.
(But remember that some options, like `--compile-all-callers`, only apply to
the `smout solve-greedy` and `smout optimize` commands.)

Suppose you run `smout candidates --max-adjacent=20`, and then you run `smout
optimize --max-adjacent=30`. The `smout optimize` command will actually ignore
the results of the `smout candidates` command, because the option value didn't
match, and it will generate new candidates using the `--max-adjacent=30`
option. So the results from both commands will be correct, but they will use
two different sets of candidate generation results.

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

#### Manually running the funcs and analyzing results

The outliner actually uses a bunch of different MemoDB funcs to handle
different parts of the outlining process:

- `smout.actual_size_vN`: compile a function and get its actual machine code
  size.
- `smout.candidates_vN`: generate outlining candidates from a particular
  original function, using specified options.
- `smout.grouped_candidates_vN`: use `smout.candidates_vN` on all functions in
  a module, then group all the candidates based on the callee function type and
  accessed global variables.
- `smout.extracted_callees_vN`: extract a list of callee functions (indicated
  by node number ranges) from a particular original function.
- `smout.extracted_caller_vN`: extract a single caller function (indicated by a
  node number range) from a particular original function.
- `smout.grouped_callees_for_function_vN`: use `smout.extracted_callees_vN`
  (possibly multiple times) on an original function and combine all the results
  with the results from `smout.grouped_candidates_vN`.
- `smout.grouped_callees_vN`: use `smout.grouped_callees_for_function_vN` on
  all functions in a module, then group the results.
- `smout.ilp_problem_vN`: BROKEN.
- `smout.greedy_solution_vN`: use `smout.grouped_callees_vN` on a module, then
  use a greedy algorithm to decide which candidates to outline. Avoids
  overlapping candidates.
- `smout.outlined_module_vN`: given a set of candidates to outline in a
  particular module (such as the set returned by `smout.greedy_solution_vN`),
  applies `smout.extracted_caller_vN` to apply outlining to all the functions
  in the module, and organizes everything into a new module compatible with the
  `bcdb get` command.
- `smout.optimized_vN`: the full outlining process. Just applies
  `smout.greedy_solution_vN` to a module and gives the result to
  `smout.outlined_module_vN`.

See [Funcs.h](include/outlining/Funcs.h) and [Funcs.cpp](lib/Funcs.cpp) for the
actual code. And remember that all the func names have version numbers, which
you can see in Funcs.cpp (for example, `smout.candidates` might actually be
called `smout.candidates_v1`).

You can use `memodb get /call` to list all funcs that currently have results in
MemoDB. You can use `memodb get /call/smout.greedy_solution` to see all the
calls of one particular func. You can use `memodb get
/call/smout.greedy_solution/...` to get the actual results of one particular
call.

If you need to get lots of results, running `memodb get` thousands of times
would be very slow. Instead, you can start `memodb-server` and write your own
script that uses the [REST API] to download results.

You can use the `smout evaluate /call/...` command to run these funcs manually.
For example, you could take the normal output of `smout.greedy_solution`,
delete all candidates from the set except one, and then apply
`smout.outlined_module` to the new set, in order to find out what happens when
you only outline one candidate.

[MemoDB tutorial]: ../memodb/docs/tutorial.md
[REST API]: ../memodb/docs/rest-api.md
