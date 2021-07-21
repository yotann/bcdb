# The Bitcode Database _(bcdb)_

[![Tests](https://github.com/yotann/bcdb-private/actions/workflows/tests.yml/badge.svg)](https://github.com/yotann/bcdb-private/actions/workflows/tests.yml)
[![Lint](https://github.com/yotann/bcdb-private/actions/workflows/lint.yml/badge.svg)](https://github.com/yotann/bcdb-private/actions/workflows/lint.yml)
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
- [LLVM](https://llvm.org/) version 10 through 12 (development versions up to
  13 may work, but this is not guaranteed)
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
  - [nng](https://github.com/nanomsg/nng), tested with 1.4, for distributed
    processing.

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

- The BCDB proper: see [BCDB CLI](docs/BCDB/cli.md).
- The Nixpkgs bitcode overlay: see [nix/bitcode-overlay](nix/bitcode-overlay/).
- The guided linking experiments: see [nix/gl-experiments](nix/gl-experiments/).
- Other subprojects: start from [docs/README.md](docs/README.md).

## Design

See [docs/README.md](docs/README.md) for more information about the design of
each subproject.

## Maintainer

[Sean Bartell](https://github.com/yotann).

## Contributing

There's no formal process for contributing. You're welcome to submit a PR or
contact
[smbarte2@illinois.edu](mailto:smbarte2@illinois.edu?subject=Bitcode%20Database).

## License

Apache License 2.0 with LLVM Exceptions, copyright 2018–2021 Sean Bartell and
other contributors. See license in [LICENSE.TXT](LICENSE.TXT).
