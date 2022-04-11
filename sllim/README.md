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

SLLIM builds on the BCDB infrastructure project, and is currently located in a
subdirectory of the BCDB repository. A Dockerfile is provided to install SLLIM
in an Ubuntu container (**recommended**):

```shell
$ git clone https://github.com/yotann/bcdb.git
$ cd bcdb
$ docker build -t sllim-ubuntu -f experiments/dockerfiles/sllim-ubuntu.docker .
...
Successfully tagged sllim-ubuntu:latest
$ docker run -it sllim-ubuntu
root@...:/#
```

As an alternative, SLLIM can easily be installed on any Linux system after
[installing Nix](https://nixos.org/download.html). You can use
[sllim-ubuntu.docker](../experiments/dockerfiles/sllim-ubuntu.docker) as a
starting point.

**WARNING:** SLLIM will start a database server (`memodb-server`) on 127.0.0.1.
There's no access control, so you need to trust every process running on
127.0.0.1. This is safe in a SLLIM-specific container or VM; other uses are a
bad idea for now.

```shell
$ git clone https://github.com/yotann/bcdb.git
$ cd bcdb
$ nix-env -f . -iA sllim
```

## Usage

Here's an example using SLLIM to optimize LZ4. Run these commands in the Docker
container.

```shell
root# git clone --depth=1 https://github.com/lz4/lz4.git
Cloning into 'lz4'...
...
root# cd lz4
root# sllim-env.sh
SLLIM overrides added.
sllim-env: root# make -j4 lz4
compiling static library

sllim-ld: optimizing lz4

sllim-env: root# size lz4
  text    data     bss     dec     hex filename
172907     940     104  173951   2a77f lz4
```

The `optimizing lz4` line shows you that SLLIM is actually working.

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

Simply run `sllim input.bc -o output.o`. Note that the input is LLVM bitcode,
while the output is an object file containing machine code.

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
and making some other tweaks. See the [sllim-cc source](bin/sllim-cc) and
[sllim-ld source](bin/sllim-ld) for details.

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

Currently, SLLIM is configured by setting the `SLLIM_LEVEL` environment variable
before using it. `SLLIM_LEVEL` affects the `sllim` command, which is invoked
when linking an executable or shared library; it has no effect when compiling
an object file. With most build systems, if you want to change the optimization
level, you just need to remove the executables/libraries, change
`SLLIM_LEVEL`, and run `make` again.

Different size optimizations are enabled at different values of `SLLIM_LEVEL`:

- `SLLIM_LEVEL=0`: Avoid optimizations but still go through SLLIM; useful for
  testing or speed.
- `SLLIM_LEVEL=1`: Enable `StandardPass` (basically `opt -Oz`).
- `SLLIM_LEVEL=2`: Enable `ForceMinSizePass` (which enables optimization for
  all functions, even if they were compiled with `-O0`).
- `SLLIM_LEVEL=3`: Enable iterated machine outlining. LLVM already enables the
  machine outliner by default for common targets, but [Uber
  added](https://ieeexplore.ieee.org/document/9370306) support for running this
  outliner multiple times in a row, getting additional benefits.
- `SLLIM_LEVEL=7`: Enable smout, our powerful (but slow) IR-level outliner.
- `SLLIM_LEVEL=8`: Make smout search more exhaustively for outlining candidates.
- `SLLIM_LEVEL=9`: Make smout compile all possible outlining candidates to
  determine their actual effects on code size, making it much more accurate.
- `SLLIM_LEVEL=10`: Enable [Google's ML-based inlining
  heuristics](https://github.com/google/ml-compiler-opt). These are supposed to
  help reduce code size, but in the few tests we've done we've observed them to
  *increase* code size, so they may be counterproductive.

**NOTE:** when smout is enabled (`SLLIM_LEVEL` 7 and up), optimization times
are much longer because smout extracts and evaluates huge numbers of outlining
candidates. This process is highly parallel, so it's recommended to use SLLIM
on a machine with many cores (for example, 32 cores).

You can use `SLLIM_LEVEL=0` for configuration (to speed up building test
programs) and a higher level for the actual code:

```shell
sllim-env: $ export SLLIM_LEVEL=0
sllim-env: $ ./configure
sllim-env: $ export SLLIM_LEVEL=9
sllim-env: $ make -j10
```

## Future Work

### Security

Instead of opening a port on 127.0.0.1, we should make `memodb-server` and the
clients support Unix sockets for access control. Time to implement: ~1 week.

### Configurability

There should be a way to configure SLLIM in more detail than just using
`SLLIM_LEVEL`. Our current plan is to let the developer use a custom Python
script that overrides `sllim.configuration.Config`. Time to implement: a few
weeks.

### Effectiveness

We're interested in making SLLIM try multiple possible optimization sequences
and choose the best result. It could either use brute force or some kind of
optimization algorithm (like [Compiler Gym](https://compilergym.com/)). Time to
implement (brute force): <1 week.

We're interested in adding support for more types of optimization, such as
"Function Merging by Sequence Alignment", TRIMMER (partial evaluation), Guided
Linking, and so on. Some of these (like TRIMMER and Guided Linking) will need
specific configuration for each project being optimized with SLLIM.

### Correctness

#### sllim

The `sllim` command itself should generally be correct for all kinds of code,
just like normal LLVM passes. But note that (depending on `SLLIM_LEVEL`) it
may force all code to be optimized, even if the developer tried to prevent it
from being optimized. And note that smout can prevent backtraces from working
normally.

#### sllim-cc

The `sllim-cc` command has to remove some options in order to make
`-fembed-bitcode` to work; see the [sllim-cc source](bin/sllim-cc) for details.
An alternative would be to use `-flto` instead of `-fembed-bitcode`, which may
be compatible with more of the options, but might confuse build systems because
the object files aren't in the usual format.

#### sllim-ld

Currently, `sllim-ld` works by running the linker command (with some inputs
that have bitcode available and some that may not), extracting and optimizing
bitcode from the result, and then running the linker command *again* with the
optimized bitcode *in addition to the original inputs*. This means the same
code is actually linked in twice; we use `-zmuldefs` to allow the duplicate
definitions and `--gc-sections` to remove the extra, unoptimized definitions.
This seems to work, but it's unsatisfying and will probably cause problems.

There are a few options to do this more correctly:

1. Process the linker command line options to figure out which inputs have
   bitcode, so we can extract it and remove them from the linker command. This
   would involve reimplementing part of the linker functionality (such as
   finding libraries and perhaps parsing linker scripts) in a shell script.
2. Implement a custom linker plugin. This would give us exactly the interface
   we need. However, it would only work for GNU `ld` and `gold`, not `lld`, and
   would make SLLIM slightly more difficult to install. Time to implement: 1–2
   weeks.
3. Extend the linkers with custom code. This would need to be done separately
   for each linker, and it would be incompatible with any modified linkers
   developers might be using. Time to implement: a few weeks per linker.

### Speed

SLLIM currently uses a lock to protect the MemoDB store, which means that only
one `sllim` process can be active at once. All the code would actually work
perfectly fine with multiple `sllim` processes connected to the same
`memodb-server` instance; the lock only exists to simplify starting and
stopping `memodb-server`. Time to implement: ~1 week.

SLLIM effectively uses normal LTO, because it links everything into one bitcode
file before optimizing it. For large projects such as libLLVM, just running the
simplest optimizations on this file can be very slow. It would be nice to
support ThinLTO as a faster alternative, even if only a subset of our
optimizations work. Time to implement: several weeks, depending on how many
optimizations need support.

SLLIM's caching could be finer-grained; instead of caching the full `opt -Oz`
invocation on a module, it could cache individual function passes on a
per-function basis, for example. Time to implement: a few weeks.

Smout is designed to support our semantic outlining work, which means it has to
extract every outlining candidate into a new function *before* determining
whether any equivalent candidates exist; if we decide to forget about semantic
equivalence and just rely on syntactic equivalence, we could avoid extracting
candidates and instead use some sort of graph equivalence searching algorithm,
which would be much faster. Smout also spends a lot of time compiling each
candidate to determine its code size, but we could heuristics to avoid doing
this. Time to implement: several weeks or longer.

### Usability

It'd be nice to use a log file for all the messages printed by different parts
of SLLIM, rather than using stdout/stderr.

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
