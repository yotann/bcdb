# SLLIM

An easy-to-use tool that applies many different code size optimizations.

The SLLIM tool takes code compiled with LLVM and applies a variety of
different code size optimizations, including LLVM's default optimizations,
LLVM optimizations that aren't enabled by default, and optimizations
from external research projects. Optimization results are cached to
avoid redundant computation. We also provide scripts that make it
easy to apply SLLIM to existing software projects without modifying
their build systems.

## Install

A Dockerfile is provided to install SLLIM in an Ubuntu container:

```shell
$ cd .../bcdb
$ docker build -t sllim-ubuntu -f experiments/dockerfiles/sllim-ubuntu.docker .
...
Successfully tagged sllim-ubuntu:latest
$ docker run -it sllim-ubuntu
root@...:/#
```

As an alternative, SLLIM can easily be installed on any Linux system after
[installing Nix](https://nixos.org/download.html). You can use
[sllim-ubuntu.docker](experiments/dockerfiles/sllim-ubuntu.docker) as a
starting point.

```shell
$ cd .../bcdb
$ nix-env -f . -iA sllim
```

## Usage

```shell
root# # In the Docker container, or somewhere else SLLIM is installed:
root# git clone --depth=1 https://github.com/lz4/lz4.git
Cloning into 'lz4'...
...
root# cd lz4
root# sllim-env.sh
SLLIM overrides added.
sllim-env: root# make -j4 lz4
compiling static library
sllim-env: root# size lz4
  text    data     bss     dec     hex filename
172907     940     104  173951   2a77f lz4
sllim-env: root# exit
root#
```

SLLIM is designed to work with any C/C++ code that can be built with Clang. In
order to make it easy to use with existing projects and diverse build systems,
three ways are provided to invoke SLLIM:

1. Use the `sllim` command directly. This is difficult as you are required to
   make your build system work with LLVM bitcode.
2. Configure your build system to use `sllim-cc` as the compiler, etc. These
   scripts should handle everything else automatically.
3. Use `sllim-env.sh` **(recommended)** before you configure and build your
   project. Most of the time, this will work automatically without any extra
   effort on your part.

### Option 1: Using `sllim` directly

Not documented yet.

### Option 2: Using `sllim-cc` etc.

```shell
$ CC=sllim-cc CXX=sllim-c++ LD=sllim-ld ./configure
$ CC=sllim-cc CXX=sllim-c++ LD=sllim-ld make
```

If you know how to override the compiler and linker used by your build system,
choosing `sllim-cc`, `sllim-c++`, and `sllim-ld` will generally allow you to
apply SLLIM to your project without any further effort. For many projects,
setting `CC`, `CXX`, and `LD` (as above) is enough.

The `sllim-cc` and `sllim-c++` wrapper scripts can be used as drop-in
replacements for `clang` and `clang++`. They work by invoking the original
`clang`/`clang++`, adding options to generate LLVM bitcode and use `sllim-ld`
and making some other tweaks.

The `sllim-ld` wrapper script can be used as a drop-in replacement for `ld`. It
works by running the original `ld` normally to produce a program or shared
library, then extracting the bitcode, optimizing it with `sllim`, and compiling
and linking the result to produce a new, size-optimized program or shared
library.

These scripts require the original `clang`, `clang++`, `ld`, `bc-imitate`
(part of BCDB), and `sllim` itself to be in your `PATH`. Alternatively, you can
set `SLLIM`, `SLLIM_BC_IMITATE`, `SLLIM_CLANG`, `SLLIM_CLANGXX`, and `SLLIM_LD`
to the full paths of each program.

### Option 3: Using `sllim-env.sh`

This is the easiest option; you just have to source `sllim-env.sh` in your
shell, and then configure and build your project in the same shell. For most
open source projects, this is all you need to do, and you don't need to deal
with the build system at all.

The `sllim-env.sh` script tries to force build systems to use `sllim-cc` and
the other scripts. Not only does it set variables like `CC`, but it also adds
`cc`, `gcc`, `clang`, etc. wrapper scripts to your `PATH`, ensuring your build
system will use `sllim-cc` even if it hardcodes the name of the compiler.

This script requires SLLIM, `clang`, `clang++`, `ld`, `bc-imitate`, and the
LLVM tools (such as `llvm-ar`) to be in your `PATH` before you start it. It
works with Bash, Zsh, BusyBox Ash, Dash, and probably other shells too.

**IMPORTANT:** Configuration tools, like `./configure` and `cmake`, often try
to compile lots of small test files to check which compiler features are
available. It's normal for some of these checks to fail, and any error messages
are usually hidden, but `sllim-env.sh` forces its error messages to be shown in
order to help with debugging. You should generally ignore these `sllim-env.sh`
errors as long as the configuration tool ignores them.

### Configuring SLLIM

Not documented yet.

## Future Work

### Support for Files Without Bitcode

Currently, `sllim-ld` requires all linker inputs to have bitcode available, or
else they will be omitted from the final link command, probably causing an
error. This can be a problem in several situations:

- Projects that use separate assembly files (`.s` files), which can't currently
  be compiled into bitcode. (Note that inline assembly in `.c` files already
  works correctly, which covers most projects. Even projects that use separate
  assembly files often have an option to disable them.)
- C files that use GCC-only extensions, and therefore can't be compiled into
  bitcode using Clang. (Clang supports all commonly used GCC extensions, but
  not some rarely used ones such as nested functions.)
- Projects that link against precompiled static libraries, which don't include
  bitcode.

We have a plan for how to support these cases by analyzing the linker arguments
and input files, possibly also using a custom linker plugin, but we haven't yet
implemented it.

### Advanced Linker Options

Similar to the previous section, `sllim-ld` lacks support for some linker
options, such as symbol visibility lists that control which symbols are
exported from a library. We plan to improve `sllim-ld` to make it support these
options.

### Ease of Installation

SLLIM depends on a variety of Python modules, custom LLVM plugins, and C++
libraries, and we feel that Nix is the best way to make these dependencies
manageable. But installing Nix could still be difficult on old systems or when
root access isn't available. There are a few ways we could make this easier:

- Use [nix-bundle](https://github.com/matthewbauer/nix-bundle) to create a
  single self-extracting executable that contains `sllim` and all its
  dependencies. This would be convenient, but it does require the kernel to
  support user namespaces (which aren't always enabled), and we haven't tested
  nix-bundle with large amounts of software.
- Use [PRoot](https://proot-me.github.io/) to install Nix without requiring
  root access or user namespaces.
- Run `sllim` on a separate server; put just the `sllim-*` scripts, which are
  highly portable, on the legacy system where software is being built, and have
  the scripts upload bitcode to the `sllim` server for optimization.

### Support for Other Languages

The `sllim` tool itself should work with any programming language that can be
compiled into LLVM bitcode, but we currently only have scripts to do this
conveniently for C and C++. It would be nice to add support for Swift, Rust,
and perhaps other languages.

### Static Libraries

SLLIM currently only optimizes the final, fully linked executables and shared
libraries, including any static libraries that they are linked against. If a
static library project wants to use SLLIM, but the library will only be used by
other projects that don't use SLLIM, there's currently no way to get the
benefits of SLLIM optimization. We could try to add some support for this case,
although optimization opportunities will be limited because we can't link
everything into one module.
