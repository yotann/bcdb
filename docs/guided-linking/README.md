# Guided Linking: Dynamic Linking Without the Costs

Sean Bartell, Will Dietz, and Vikram S. Adve. 2020. Guided Linking: Dynamic
Linking without the Costs. *Proc. ACM Program. Lang.* 4, OOPSLA, Article 145
(November 2020), 29 pages. <https://doi.org/10.1145/3428213>

[Paper PDF](./paper.pdf), [Talk Video](https://youtu.be/QyNJKllZP4I), [Poster
PDF](./poster.pdf), [Poster Video](https://youtu.be/GQR9W44N5W4).

## About

Guided linking is a technique to perform arbitrary optimizations on dynamically
linked code. For example, a function from a shared library can be inlined into
a plugin. See the links above to learn more about the technique.

Our implementation of guided linking is integrated into the BCDB, as the
command `bcdb gl`. The main body of the guided linking code is in
[`lib/BCDB/GuidedLinker.cpp`](../../lib/BCDB/GuidedLinker.cpp). It's built on
[`lib/BCDB/Merge.cpp`](../../lib/BCDB/Merge.cpp), which is used to merge
modules together and handle symbol conflicts.

## Performing guided linking with Nix

The recommended way to use the tool is to install [Nix](https://nixos.org/) and
use our Nix expressions, which automatically build Linux packages in bitcode
form and apply guided linking. Simple builds can be set up with less than 10
lines of code. For more details, see
[nix/gl-experiments](../../nix/gl-experiments).

## Performing guided linking manually

These instructions assume you have built the BCDB and installed it in `PATH`.

### 1. Build with bitcode

```sh
git clone https://github.com/lz4/lz4
cd lz4
CC=clang make CFLAGS=-fembed-bitcode
```

### 2. Extract the bitcode

This extracts the bitcode that was saved with `-fembed-bitcode`, and also adds
some extra information, like the list of libraries that `lz4` is dynamically
linked to.

```sh
bc-imitate annotate --binary=lz4 > lz4.bc
```

### 3. Initialize the BCDB file

```sh
export MEMODB_STORE=sqlite:lz4.bcdb
memodb init
bcdb add -name lz4 lz4.bc
```

Behind the scenes, the `lz4.bc` bitcode module is split into separate
functions, and syntactically identical functions are deduplicated.

### 4. Explore the BCDB file (optional)

```sh
bcdb list-modules
bcdb list-function-ids
memodb paths-to id:99
bcdb get-function --id=99 | llvm-dis
```

### 5. Perform guided linking

```sh
bcdb gl --noplugin --nouse lz4 --merged-name merged -o out
llvm-dis < out/lz4
llvm-dis < out/muxed
```

### 6. Compile to machine code

We use `bc-imitate` to figure out any extra linker options we need to add;
for instance, if the original `lz4` file linked against a library `libfoo.so`,
`bc-imitate clang-args out/lz4` will output `-lfoo`.

```sh
clang -flto out/merged -o out/merged.so $(bc-imitate clang-args out/merged)
clang -flto out/lz4 out/merged.so -o out/lz4.exe $(bc-imitate clang-args out/lz4)
out/lz4.exe --help
```
