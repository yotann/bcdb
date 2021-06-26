# BCDB

[![Github Workflows](https://github.com/yotann/bcdb/workflows/Test/badge.svg)](https://github.com/yotann/bcdb/actions?query=workflow%3ATest)
[![ALLVM ALL THE THINGS!](https://img.shields.io/badge/ALLVM-ALL%20THE%20THINGS-brightgreen.svg)](https://github.com/allvm/allvm-tools)
[![Cachix cache](https://img.shields.io/badge/cachix-bcdb-blue.svg)](https://bcdb.cachix.org)

The Bitcode Database (BCDB) is a research tool developed as part of the ALLVM
Project at UIUC. Features:

- Stores huge amounts of LLVM bitcode in an SQLite database.
- Automatically deduplicates bitcode at the function level.
- Stores results of analyzing and processing bitcode (WIP).
- Performs Guided Linking, which optimizes dynamically linked code. See
  [`docs/guided-linking`](docs/guided-linking/) for details and instructions.

## Building

### Build automatically with Nix

If you have [Nix](https://nixos.org/guides/install-nix.html) installed, it can
automatically build BCDB along with its dependencies. See `default.nix` for the
list of attributes you can build. For example:

```shell
git submodule update --init --depth=1
nix-build -A bcdb
result/bin/bcdb -help
```

If you want to modify the BCDB code, you can also have Nix build just the
dependencies and enter a shell with them installed:

```shell
git submodule update --init --depth=1
nix-shell -A bcdb
mkdir build
cd build
cmake ..
make
```

You need to make sure you're in the `nix-shell` shell every time you want to
build BCDB this way. The [direnv](https://direnv.net/) tool can help set this
up automatically.

In any case, you can speed up Nix by using our [Cachix](https://cachix.org)
cache, which includes prebuilt versions of LLVM. Simply install Cachix and run
`cachix use bcdb`.

### Build manually with CMake

You will need the following dependencies:

- C++ compiler with C++17 support.
- [LLVM](https://llvm.org/) version 9 through 12 (development versions up to 13
  may work, but this is not guaranteed)
  - When working on the BCDB code, you should make sure LLVM is built with
    assertions enabled.
  - LLVM's `FileCheck` and `not` programs must be installed as well. Some
    packages (including some of LLVM's official packages) exclude these
    programs or split them off into a separate package.
- [Clang](https://clang.llvm.org/), same version as LLVM.
- [CMake](https://cmake.org/), at least version 3.13.
- [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/)
- [Libsodium](https://libsodium.org/)
- [SQLite](https://sqlite.org/)
- [Python](https://www.python.org/), at least 3.6.
- Optional dependencies:
  - [RocksDB](https://rocksdb.org/), preferably at least 6.19, with LZ4 and
    Zstandard support (`ROCKSDB_LITE` is not supported).
  - [nng](https://github.com/nanomsg/nng), tested with 1.4, for outlining.

You should then be able to build normally with CMake, like this:

```shell
git submodule update --init --depth=1
mkdir build
cd build
cmake ..
make
make check
```

## Using the BCDB

### Basic operation

BCDB stores all its data in an SQLite database. Every time you run the `bcdb`
command, you must give it the path to the database using the option `-store
sqlite:path/to/my.bcdb`. You must initialize the database before you can use
it:

```shell
bcdb init -store sqlite:example.bcdb
```

You can add whatever LLVM bitcode modules you like to the database. Each module
is given a unique name; by default, the name is the filename you give to `bcdb
add`.

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

You can also delete modules, but garbage collection has not been implemented
yet, so no disk space will actually be freed.

```shell
bcdb delete -store sqlite:example.bcdb -name empty
```

### Working with functions

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
bcdb refs -store sqlite:example.bcdb 0
```

### Other subcommands

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
2. `bc-imitate clang -O2 annotated.bc -o output.elf`

## Contact

**Mailing List**: https://lists.cs.illinois.edu/lists/info/allvm-dev

Everyone is welcome to join!
