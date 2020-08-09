# NOTE: when updating this file, run utils/cache-deps.sh to upload dependencies
# to Cachix so CI builds can use them.

{ nixpkgs ? fetchTarball "https://github.com/NixOS/nixpkgs/archive/d44a752b91e896c499e37036eb8f58236eeac750.tar.gz" }:

let
  debugLLVM = llvmPackages: (llvmPackages.llvm.override {
    debugVersion = true;
  }).overrideAttrs (o: {
    doCheck = false;
  });
in

with import nixpkgs {};
rec {
  bcdb-llvm6 = callPackage ./package.nix {
    llvm = debugLLVM llvmPackages_6;
    clang = clang_6;
  };
  bcdb-llvm7 = callPackage ./package.nix {
    llvm = debugLLVM llvmPackages_7;
    clang = clang_7;
  };
  bcdb-llvm8 = callPackage ./package.nix {
    llvm = debugLLVM llvmPackages_8;
    clang = clang_8;
  };
  bcdb-llvm9 = callPackage ./package.nix {
    llvm = debugLLVM llvmPackages_9;
    clang = clang_9;
  };
  bcdb-llvm10 = callPackage ./package.nix {
    llvm = debugLLVM llvmPackages_10;
    clang = clang_10;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  bcdb-clang = callPackage ./package.nix {
    inherit (llvmPackages_10) stdenv llvm clang;
  };

  bcdb = bcdb-llvm10;
}
