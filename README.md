# The Bitcode Database _(bcdb)_

[![Tests](https://github.com/yotann/bcdb/actions/workflows/tests.yml/badge.svg)](https://github.com/yotann/bcdb/actions/workflows/tests.yml)
[![Cachix cache](https://img.shields.io/badge/cachix-bcdb-blue.svg)](https://bcdb.cachix.org)
[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)

A database and infrastructure for distributed processing of LLVM bitcode.

The Bitcode Database (BCDB) is a research tool being developed as part of the
[ALLVM Project](https://publish.illinois.edu/allvm-project/) at UIUC. It has
the following subprojects:

- [MemoDB](./memodb): A content-addressable store and a memoizing distributed
  processing framework backed by SQLite or RocksDB. It can cache the results of
  various analyses and optimizations.
- [BCDB proper](./bcdb): Builds on MemoDB to store massive amount of LLVM
  bitcode and automatically deduplicate the bitcode at the function level. All
  the other subprojects are build on BCDB.
- [Guided Linking](./guided_linking): A tool that can optimize dynamically
  linked code as though it were statically linked.
- [Outlining](./outlining): A work-in-progress optimization to reduce code size
  using outlining.
- [SLLIM](./sllim): A easy-to-use tool to apply various code size optimizations
  (including our outliner) to existing software without messing around with
  build systems.
- [Nix bitcode overlay](./nix/bitcode-overlay): Nix expressions to
  automatically build lots of Linux packages in the form of LLVM bitcode.

## Table of Contents

- [Background](#background)
- [Install](#install)
  - [Dependencies](#dependencies)
  - [Building dependencies automatically with Nix](#building-dependencies-automatically-with-nix)
- [Usage](#usage)
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
mkdir build
cd build
cmake ..
make
make check
```

### Dependencies

Building BCDB without Nix is not officially supported. If you want to try it
anyway, you'll need to install these dependencies first:

- C++ compiler with C++17 support.
- [LLVM](https://llvm.org/) version 11 through 13 (development versions up to
  14 may work, but this is not guaranteed)
  - LLVM must be built with exception handling support. Official packages have
    this disabled, so you'll need to build LLVM yourself with `cmake
    -DLLVM_ENABLE_EH=ON` (or use Nix).
  - LLVM's `FileCheck` and `not` programs must be installed as well. Some
    packages (including some of LLVM's official packages) exclude these
    programs or split them off into a separate package.
  - When working on the BCDB code, you should make sure LLVM is built with
    assertions enabled (`cmake -DLLVM_ENABLE_ASSERTIONS=ON`).
- [Clang](https://clang.llvm.org/), same version as LLVM.
  - Clang doesn't need exception handling or assertions, so you can use an
    official Clang package.
- [CMake](https://cmake.org/), at least version 3.13.
- [Libsodium](https://libsodium.org/)
- [SQLite](https://sqlite.org/)
- [Python](https://www.python.org/), at least 3.6.
- [Boost](https://boost.org/), at least 1.75.
- Optional dependencies:
  - [RocksDB](https://rocksdb.org/), preferably at least 6.19, with LZ4 and
    Zstandard support (`ROCKSDB_LITE` is not supported).

### Building dependencies automatically with Nix

If you have [Nix](https://nixos.org/guides/install-nix.html) installed, it can
automatically build BCDB along with known-working versions of its dependencies.
See `default.nix` for the list of attributes you can build. For example:

```shell
nix-build -A bcdb
result/bin/bcdb -help
```

If you want to modify the BCDB code, you can instead build just the
dependencies with Nix, and enter a shell that has them installed:

```shell
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

See the subproject subdirectories for usage instructions and more documentation.

## Maintainer

[Sean Bartell](https://github.com/yotann).

## Contributing

There's no formal process for contributing. You're welcome to submit a PR or
contact
[smbarte2@illinois.edu](mailto:smbarte2@illinois.edu?subject=Bitcode%20Database).

## License

Apache License 2.0 with LLVM Exceptions, copyright 2018–2022 Sean Bartell and
other contributors. See license in [LICENSE.TXT](LICENSE.TXT).
