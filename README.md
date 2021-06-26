# The Bitcode Database _(bcdb)_

[![Tests](https://github.com/yotann/bcdb-private/actions/workflows/tests.yml/badge.svg)](https://github.com/yotann/bcdb-private/actions/workflows/tests.yml)
[![Cachix cache](https://img.shields.io/badge/cachix-bcdb-blue.svg)](https://bcdb.cachix.org)
[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)

A database and infrastructure for distributed processing of LLVM bitcode.

The Bitcode Database (BCDB) is a research tool being developed as part of the
[ALLVM Project](https://publish.illinois.edu/allvm-project/) at UIUC. Features:

- Stores huge amounts of LLVM bitcode in an SQLite or RocksDB database.
- Automatically deduplicates bitcode at the function level.
- Performs Guided Linking, which can optimize dynamically linked code as though
  it were statically linked. See [`docs/guided-linking`](docs/guided-linking/)
  for details and instructions.
- Caches the results of analyses and optimizations.
- Supports distributed computing for a code outlining optimization.

## Table of Contents

- [Background](#background)
- [Install](#install)
  - [Dependencies](#dependencies)
  - [Building dependencies automatically with Nix](#building-dependencies-automatically-with-nix)
- [Usage](#usage)
  - [CLI](#cli)
    - [Basic operation](#basic-operation)
    - [Working with functions](#working-with-functions)
    - [Other subcommands](#other-subcommands)
    - [Other tools](#other-tools)
  - [Nixpkgs bitcode overlay](#nixpkgs-bitcode-overlay)
  - [Guided linking experiments](#guided-linking-experiments)
- [Design](#design)
  - [MemoDB](#memodb)
  - [BCDB core](#bcdb-core)
  - [Guided linking](#guided-linking)
  - [Semantic outlining](#semantic-outlining)
- [Maintainer](#maintainer)
- [License](#license)

## Background

The BCDB has been developed primarily by [Sean Bartell](https://github.com/yotann),
to support his PhD research on code size optimization.

This project initially grew out of the [ALLVM
Project](https://publish.illinois.edu/allvm-project/), started by [Will
Dietz](https://wdtz.org/), which aims to explore the new possibilities that
would be enabled if all software on a computer system were shipped in the form
of LLVM IR.
The BCDB was originally designed as a way to store massive amounts of LLVM IR
more efficiently by using deduplication.

## Install

```shell
git submodule update --init --depth=1
mkdir build
cd build
cmake ..
make
make check
```

### Dependencies

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
- [Libsodium](https://libsodium.org/)
- [SQLite](https://sqlite.org/)
- [Python](https://www.python.org/), at least 3.6.
- Optional dependencies:
  - [RocksDB](https://rocksdb.org/), preferably at least 6.19, with LZ4 and
    Zstandard support (`ROCKSDB_LITE` is not supported).
  - [nng](https://github.com/nanomsg/nng), tested with 1.4, for outlining.

### Building dependencies automatically with Nix

If you have [Nix](https://nixos.org/guides/install-nix.html) installed, it can
automatically build BCDB along with known-working versions of its dependencies.
See `default.nix` for the list of attributes you can build. For example:

```shell
git submodule update --init --depth=1
nix-build -A bcdb
result/bin/bcdb -help
```

If you want to modify the BCDB code, you can instead build just the
dependencies with Nix, and enter a shell that has them installed:

```shell
git submodule update --init --depth=1
nix-shell -A bcdb
mkdir build
cd build
cmake ..
make
```

If you install and enable [direnv](https://direnv.net/), it will effectively
set up the Nix shell every time you enter the bcdb directory.

In any case, you can speed up Nix by using our [Cachix](https://cachix.org)
cache, which includes prebuilt versions of LLVM. Simply install Cachix and run
`cachix use bcdb`.

## Usage

### CLI

#### Basic operation

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

#### Working with functions

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

#### Other subcommands

Run `bcdb -help` for a list of the other subcommands.

#### Other tools

##### bc-align

`bc-align` rewrites a piece of LLVM bitcode so that all of the fields are
aligned to byte boundaries. The output is semantically identical to the input,
and is fully compatible with LLVM, but it compresses better with tools such as
`gzip`.

Usage: `bc-align <input.bc >aligned.bc` or `bc-align -o aligned.bc input.bc`.

##### bc-split and bc-join

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

##### bc-imitate

`bc-imitate` copies certain linking information from an ELF binary into an LLVM
module, so that the LLVM module can be properly linked into a new ELF binary.
In particular, it keeps track of which shared libraries the module should link
against. There are two steps: `bc-imitate annotate` adds the necessary linking
information to a bitcode module, and `bc-imitate clang` performs the actual
linking (using `clang`).

Usage:

1. `bc-imitate annotate input.bc -binary input.elf -o annotated.bc`
2. `bc-imitate clang -O2 annotated.bc -o output.elf`

### Nixpkgs bitcode overlay

This allows many Linux packages to be automatically built in LLVM bitcode form.
See [nix/bitcode-overlay](nix/bitcode-overlay/).

### Guided linking experiments

See [nix/gl-experiments](nix/gl-experiments/).

## Design

The BCDB project is comprised of several parts:

- MemoDB, which is a general-purpose content-addressable store.
- The BCDB core, which splits LLVM bitcode modules into pieces and uses MemoDB
  to deduplicate and store them.
- Optimizations and tools built on the BCDB core: specifically, guided linking
  and semantic outlining.

### MemoDB

This is a content-addressable store than can be backed by either SQLite or
RocksDB. Data is automatically deduplicated as it is added to the store. Data
that can be stored includes raw binary data, structured data, and cached call
results; see [MemoDB data model](docs/memodb/data-model.md) for more
information.

### BCDB core

The BCDB core is responsible for taking LLVM modules and splitting out each
function definition into its own module. It then stores each of these modules
in MemoDB, along with other information needed to recreate the original module.
MemoDB will automatically deduplicate the function definitions whenever two
of them are identical, no matter whether they came from the same LLVM module or
from different modules. The BCDB core can also reverse the process, retrieving
the parts of a module from MemoDB and reconstituting the original module.

The BCDB core makes a few changes to the LLVM IR in order to improve
deduplication:

- Names are assigned to all anonymous global variables and functions.
- Global string constants are renamed based on a hash of their contents.
- Pointers to structures are replaced by opaque pointers (i.e., void pointers)
  when the details of the structure don't matter.

Aside from these changes, the reconstituted LLVM module is expected to be
semantically identical to the original module.

### Guided linking

See [`docs/guided-linking`](docs/guided-linking/).

### Semantic outlining

This is a work in progress and is not documented yet.

## Maintainer

[Sean Bartell](https://github.com/yotann).

## Contributing

There's no formal process for contributing. You're welcome to submit a PR or
contact
[smbarte2@illinois.edu](mailto:smbarte2@illinois.edu?subject=Bitcode%20Database).

## License

Apache License 2.0 with LLVM Exceptions, copyright 2018–2021 Sean Bartell and
other contributors. See license in [LICENSE.TXT](LICENSE.TXT).
