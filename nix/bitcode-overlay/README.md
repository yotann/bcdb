# Bitcode overlay for Nixpkgs

This Nixpkgs overlay can automatically build many Linux packages with LLVM bitcode included. Use it like this:

```shell
# Build the "hello" program with embedded bitcode.
cd bcdb/nix/bitcode-overlay
nix-build -A pkgsBitcode.hello

# The embedded bitcode is stored in the ".llvmbc" section.
objdump -h result/bin/hello | grep llvmbc

# Use bc-imitate to extract and combine the bitcode,
# and add some metadata about dynamic linking.
bc-imitate extract result/bin/hello > hello.bc
```

## Limitations

Many packages don't work out-of-the-box with Clang, or with its `-fembed-bitcode` option.
This overlay provides fixes for a few packages, but many others will fail to build.

## Building bitcode without the overlay

If you want to avoid Nix, you can also build bitcode manually by using the
`-fembed-bitcode` option for Clang. For example:

```sh
git clone https://github.com/lz4/lz4
cd lz4

# The exact options you need will vary from package to package.
make CC=clang CFLAGS=-fembed-bitcode

# Extract the compiled bitcode.
bc-imitate extract lz4 > lz4.bc
```

## How the overlay works

See [design.md](./design.md) for an explanation.
