# BCDB CLI

## Basic operation

BCDB stores all its data in an SQLite or RocksDB database. SQLite is
recommended when starting out. Every time you run the `bcdb` command, you must
give it the path to the database using the option `-store
sqlite:path/to/my.bcdb`. You must initialize the database before you can use
it:

```shell
memodb init -store sqlite:example.bcdb
```

You can add whatever LLVM bitcode modules you like to the database. Each module
is given a name; by default, the name is the filename you give to `bcdb add`.

```shell
llvm-as </dev/null >empty.bc
bcdb add -store sqlite:example.bcdb -name empty empty.bc
bcdb add -store sqlite:example.bcdb /tmp/x.bc # implied -name is /tmp/x.bc
```

You can list all the modules stored in the database, and retrieve any module:

```shell
bcdb list-modules -store sqlite:example.bcdb
bcdb get -store sqlite:example.bcdb -name empty | llvm-dis
bcdb get -store sqlite:example.bcdb -name /tmp/x.bc -o /tmp/x2.bc
```

## Working with functions

When you add a module to the BCDB, it's actually split into a number of smaller
modules, one for each function definition, along with a “remainder” module that
includes global variables and linking information. Each of these smaller
modules is deduplicated with other modules already in the database, so if you
add two identical modules to the database, only one copy of each function
definition will be stored.

You can list and retrieve the individual function modules as follows:

```shell
# List all function modules in the BCDB.
bcdb list-function-ids -store sqlite:example.bcdb
# List the function modules that were extracted from a specific input module.
bcdb list-function-ids -store sqlite:example.bcdb -name /tmp/x.bc
# Retrieve a single function as an LLVM bitcode module.
bcdb get-function -store sqlite:example.bcdb -id 0 -o /tmp/function0.bc
# Find out where a particular function came from.
memodb paths-to -store sqlite:example.bcdb id:0
```

## Other subcommands

Run `bcdb -help` for a list of the other subcommands.

## Other tools

### bc-align

`bc-align` rewrites a piece of LLVM bitcode so that all of the fields are
aligned to byte boundaries. The output is semantically identical to the input,
and is fully compatible with LLVM, but it compresses better with tools such as
`gzip`.

Usage: `bc-align <input.bc >aligned.bc` or `bc-align -o aligned.bc input.bc`.

### bc-split and bc-join

`bc-split` splits a single bitcode module into a separate module for each
function, just like BCDB, but instead of using a database it just stores the
output modules as separate files in a directory. You can use it to quickly
extract functions from a module. However, in practice, many LLVM modules
(especially modules compiled from C++ code) have function names that are too
long to be used as filenames, so `bc-split` will fail.

Usage: `bc-split -o output_directory input.bc`.

`bc-join` complements `bc-split` by joining a directory of bitcode modules into
a single module. Note that it only works correctly on directories produced by
`bc-split`.

Usage: `bc-split -o output.bc input_directory`.

### bc-imitate

`bc-imitate` copies certain linking information from an ELF binary into an LLVM
module, so that the LLVM module can be properly linked into a new ELF binary.
In particular, it keeps track of which shared libraries the module should link
against. There are two steps: `bc-imitate annotate` adds the necessary linking
information to a bitcode module, and `bc-imitate clang` performs the actual
linking (using `clang`).

Usage:

1. `bc-imitate annotate input.bc -binary input.elf -o annotated.bc`
2. Perform arbitrary optimizations on `annotated.bc`
3. `bc-imitate clang -O2 annotated.bc -o output.elf`

Alternatively, if you have an ELF binary with embedded LLVM bitcode (via `clang
-fembed-bitcode` or `swiftc -embed-bitcode`), you can use `bc-imitate extract`
to extract and annotate the bitcode in a single step.

Usage:

1. `bc-imitate extract input.elf -o annotated.bc`
2. Perform arbitrary optimizations on `annotated.bc`
3. `bc-imitate clang -O2 annotated.bc -o output.elf`
