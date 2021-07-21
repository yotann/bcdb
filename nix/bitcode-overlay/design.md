# How the bitcode overlay works

For an explanation of overlays in general, see:

- [Nixpkgs manual on defining overlays](https://nixos.org/manual/nixpkgs/stable/#sec-overlays-definition)
- [NixOS wiki on overlays](https://nixos.wiki/wiki/Overlays)

The [default.nix](./default.nix) file simply takes a fixed version of Nixpkgs
and applies the overlay to it.

The actual overlay is in [bitcode.nix](./bitcode.nix). It has two layers.

## The outer layer

The outer layer adds definitions of `bitcodeWrapper`, `bitcodeStdenv`,
and `pkgsBitcode` to Nixpkgs.

`bitcodeWrapper` provides scripts named
`gcc` and `clang` that add the `-fembed-bitcode` option before running the real `clang` executable.
The scripts also rewrite some other options to work better with `-fembed-bitcode`.

`bitcodeStdenv` provides a new version of the standard build environment,
`stdenv`, where the compiler `stdenv.cc` has been replaced by `bitcodeWrapper`.
Any package built with `bitcodeStdenv` will use `bitcodeWrapper`'s `gcc` and `clang`
scripts, causing it to embed bitcode in the programs and libraries it installs.
`bitcodeStdenv` also changes some other stuff for compatibility.

`pkgsBitcode` is the result of applying the inner layer to Nixpkgs, as described below.

It's important to note that the outer layer does **not** change anything else in Nixpkgs.
So if you run `nix-build -A hello`, you'll get the original version of `hello` without
embedded bitcode or any other changes. The only changes in this layer are the three new attributes
`bitcodeWrapper`, `bitcodeStdenv`, and `pkgsBitcode`.

The outer layer also defines `original` to refer to the final version of Nixpkgs.
This allows the inner layer to refer to the original, non-bitcode versions of packages.

## The inner layer

The inner layer (inside `self.extend`) creates a modified version of Nixpkgs that gets
stored as `pkgsBitcode`. The core of this layer is the line
`stdenv = original.bitcodeStdenv;`, which overrides the normal build environment with
a special one that embeds bitcode in all programs and libraries.
Everything else is just patches and helper functions to fix packages
that need some help working with `bitcodeStdenv`.

When you build one of the packages in `pkgsBitcode`, not only that package but also
all its dependencies will be built using `bitcodeStdenv` and have bitcode embedded.
One exception is a handful of packages included in Nixpkgs' "bootstrap",
such as `zlib`, which are not built with the normal `stdenv` and will not have bitcode embedded.

In some cases, a package is broken with `bitcodeStdenv` and can't be easily fixed.
For those packages, we add lines like `inherit (original) elfutils;` towards the end of the file,
preventing the use of `bitcodeStdenv` for these packages and allowing other packages
that depend on them to be built with bitcode.
