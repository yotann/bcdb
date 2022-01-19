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

A Dockerfile is provided:

```shell
$ cd .../bcdb
$ docker build -t sllim-ubuntu -f experiments/dockerfiles/sllim-ubuntu.docker .
...
Successfully tagged sllim-ubuntu:latest
$ docker run -it sllim-ubuntu
```

SLLIM can easily be installed on any Linux system after
[installing Nix](https://nixos.org/download.html). You can use
[sllim-ubuntu.docker](experiments/dockerfiles/sllim-ubuntu.docker)
as a starting point.

## Usage

```shell
root# git clone --depth=1 https://github.com/lz4/lz4.git
Cloning into 'lz4'...
...
root# cd lz4
root# sllim-env
root# make -j4 lz4
compiling static library
root# size lz4
  text    data     bss     dec     hex filename
172907     940     104  173951   2a77f lz4
```

SLLIM is designed to work with any C/C++ code that can be built with Clang.
In order to make it easy to use with existing projects and diverse build systems,
three ways are provided to invoke SLLIM:

1. Use the `sllim` command directly. This is difficult as you are
   reequired to compile your code into an LLVM bitcode file before
   running `sllim`, and compile the resulting bitcode with the correct linker options.
2. Configure your build system to use `sllim-cc` as the compiler
   and `sllim-ld` as the linker. These scripts should handle
   everything else automatically.
3. Use `sllim-env` **(recommended)** before you configure and build
   your project. Most of the time, this will work automatically without
   any extra effort on your part.

### Option 1: Using `sllim` directly

Not documented yet.

### Option 2: Using `sllim-cc` and `sllim-ld`

```shell
$ CC=sllim-cc CXX=sllim-c++ LD=sllim-ld make
```

The `sllim-cc` and `sllim-c++` wrapper scripts can be used as drop-in
replacements for `clang` and `clang++`. They adjust their arguments
to enable LLVM bitcode, override the linker with `sllim-ld`, and make
some other tweaks, and then invoke `clang`/`clang++`.

The `sllim-ld` wrapper script can be used as a drop-in replacement for `ld`.
It first runs `ld` normally to produce a program or shared library,
then extracts the bitcode, optimizes it with `sllim`, and compiles
and links the result to produce a new, size-optimized program or shared library.

### Option 3: Using `sllim-env`

The `sllim-env` script updates various environment variables,
adds `cc`, `gcc`, `clang`, etc. programs to the `PATH`,
and then starts a new shell in which you can configure and build
your project. The goal is to ensure that no matter what build
system is being used, it will use `sllim-cc`/`sllim-c++` for
compilation and `sllim-ld` for linking. For most open source
projects, this is all you need to do, and you don't need to
modify the build system at all.

### Configuring SLLIM

Not documented yet.

## Future Work

### Support for Files Without Bitcode

Currently, `sllim-ld` requires all linker inputs to have bitcode available,
or else they will be left out of the final link command, probably causing
an error. This can be a problem in several situations:

- Projects that use separate assembly files (`.s` files), which can't
  currently be compiled into bitcode. (Note that inline assembly in `.c` files
  already works correctly, and projects that use separate assembly files
  often have an option to disable them.)
- C files that use GCC-only extensions, and therefore can't be compiled
  into bitcode using Clang. (Clang supports all commonly used GCC extensions,
  but not some rarely used ones such as nested functions.)
- Projects that link against precompiled static libraries, which don't
  include bitcode.

We have some idea how to support these cases by analyzing the linker
arguments and input files, possibly also using a custom linker plugin,
but we haven't yet implemented them.

### Advanced Linker Options

Similar to the previous section, `sllim-ld` lacks support for some linker options,
such as symbol visibility lists that are sometimes used to control which
symbols are exported from a library. We plan to improve `sllim-ld` to make it
support these options.

### Ease of Installation

SLLIM depends on a variety of Python modules, custom LLVM plugins,
and C++ libraries, and we feel that Nix is the best way to make it
reasonably easy to install. But installing Nix could still be difficult
on old systems or when root access isn't available.
There are a few ways we could make this easier:

- Use [nix-bundle](https://github.com/matthewbauer/nix-bundle) to create a single self-extracting executable
  that contains `sllim` and all its dependencies. This would be convenient,
  but it does require the kernel to support user namespaces (which aren't always enabled),
  and we haven't tested nix-bundle with such large sets of software.
- Use [PRoot](https://proot-me.github.io/) to install Nix without requiring root access or user namespaces.
- Run `sllim` on a separate server; put just the `sllim-*` scripts,
  which are highly portable, on the legacy system where software is being built,
  and have the scripts upload bitcode to the `sllim` server for optimization.

### Support for Other Languages

The `sllim` tool itself should support any programming language
that can be compiled into LLVM bitcode, but we currently only
have scripts to do this conveniently for C and C++. It would be
nice to add support for Swift, Rust, and perhaps other languages.

### Static Libraries

SLLIM currently only optimizes the final, fully linked executables
and shared libraries, including any static libraries that they are linked against.
If a static library project wants to use SLLIM, but the library will only be used by
other projects that don't use SLLIM, there's currently no way to get the
benefits of SLLIM optimization. We could try to add some support for this case,
although optimization opportunities will be limited because
we can't link everything into one module.
