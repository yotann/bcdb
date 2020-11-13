# BCDB

[![Github Workflows](https://github.com/yotann/bcdb/workflows/Test/badge.svg)](https://github.com/yotann/bcdb/actions?query=workflow%3ATest)
[![Semaphore](https://bcdb.semaphoreci.com/badges/bcdb.svg?key=93e3989a-c2bb-49ac-96a0-3d92601b9fed)](https://bcdb.semaphoreci.com/projects/bcdb)
[![ALLVM ALL THE THINGS!](https://img.shields.io/badge/ALLVM-ALL%20THE%20THINGS-brightgreen.svg)](https://github.com/allvm/allvm-tools)
[![Cachix cache](https://img.shields.io/badge/cachix-bcdb-blue.svg)](https://bcdb.cachix.org)

## Building

### Build automatically with Nix

If you have [Nix](https://nixos.org/nix/) installed, it can automatically build
BCDB along with its dependencies. See `default.nix` for the list of attributes
you can build. For example:

```shell
nix-build -A bcdb-llvm10
result/bin/bcdb -help
```

If you want to modify the BCDB code, you can also have Nix build just the
dependencies and enter a shell with them installed:

```shell
nix-shell -A bcdb-llvm10
mkdir build
cd build
cmake ..
make
```

You need to make sure you're in the `nix-shell` shell every time you want to
build BCDB this way.

In any case, you can speed up Nix by using our [Cachix](https://cachix.org)
cache, which includes prebuilt versions of LLVM. Simply install Cachix and run
`cachix use bcdb`.

### Build manually with CMake

You will need the following dependencies:

- [CMake](https://cmake.org/), a recent version.
- [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/)
- [Libsodium](https://libsodium.org/)
- [SQLite](https://sqlite.org/)
- [Python](https://www.python.org/), at least 2.7.
- [LLVM](https://llvm.org/) version 6 through 10 (development versions up to 11
  may work, but this is not guaranteed). When working on the BCDB code, you
  should make sure LLVM is built with assertions enabled.
- [Clang](https://clang.llvm.org/), same version as LLVM.
- `xxd`, which is usually included with [Vim](https://www.vim.org/)

You should then be able to build normally with CMake, like this:

```shell
mkdir build
cd build
cmake ..
make
make check
```

## Using the BCDB

### Basic operation

BCDB stores all its data in an SQLite database. Every time you run the `bcdb`
command, you must give it the path to the database using the option `-uri
sqlite:path/to/my.bcdb`. You must initialize the database before you can use
it:

```shell
bcdb init -uri sqlite:example.bcdb
```

You can add whatever LLVM bitcode modules you like to the database. Each module
is given a unique name; by default, the name is the filename you give to `bcdb
add`.

```shell
llvm-as </dev/null >empty.bc
bcdb add -uri sqlite:example.bcdb -name empty empty.bc
bcdb add -uri sqlite:example.bcdb /tmp/x.bc # implied -name is /tmp/x.bc
```

You can list all the modules stored in the database, and retrieve any module:

```shell
bcdb list-modules -uri sqlite:example.bcdb
bcdb get -uri sqlite:example.bcdb -name empty | llvm-dis
bcdb get -uri sqlite:example.bcdb -name /tmp/x.bc -o /tmp/x2.bc
```

You can also delete modules, but garbage collection has not been implemented
yet, so no disk space will actually be freed.

```shell
bcdb delete -uri sqlite:example.bcdb -name empty
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
bcdb list-function-ids -uri sqlite:example.bcdb
# List the function modules that were extracted from a specific input module.
bcdb list-function-ids -uri sqlite:example.bcdb -name /tmp/x.bc
# Retrieve a single function as an LLVM bitcode module.
bcdb get-function -uri sqlite:example.bcdb -id 0 -o /tmp/function0.bc
# Find out where a particular function came from.
bcdb refs -uri sqlite:example.bcdb 0
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
