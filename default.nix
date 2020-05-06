# NOTE: when updating this file, run utils/cache-deps.sh to upload dependencies
# to Cachix so CI builds can use them.

let
  default_nixpkgs = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs";
    rev = "d44a752b91e896c499e37036eb8f58236eeac750";
    sha256 = "075xzn25jza3g82gii340223s6j0z36dvwsygssaq32s7f3h8wj5";
  };
  debugLLVM = llvmPackages: (llvmPackages.llvm.override {
    debugVersion = true;
  }).overrideAttrs (o: {
    doCheck = false;
  });
in
{ nixpkgs ? default_nixpkgs }:

with import nixpkgs {};
rec {
  bcdb-llvm6 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_6;
    clang = clang_6;
  };
  bcdb-llvm7 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_7;
    clang = clang_7;
  };
  bcdb-llvm8 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_8;
    clang = clang_8;
  };
  bcdb-llvm9 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_9;
    clang = clang_9;
  };
  bcdb-llvm10 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_10;
    clang = clang_10;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  bcdb-clang = callPackage ./build.nix {
    inherit (llvmPackages_10) stdenv llvm clang;
  };

  bcdb = bcdb-llvm10;
}
