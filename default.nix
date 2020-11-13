# NOTE: when updating this file, run utils/cache-deps.sh to upload dependencies
# to Cachix so CI builds can use them.

let
  default_nixpkgs = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs";
    rev = "d44a752b91e896c499e37036eb8f58236eeac750";
    sha256 = "075xzn25jza3g82gii340223s6j0z36dvwsygssaq32s7f3h8wj5";
  };
  nixpkgs_llvm4 = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs-channels";
    rev = "cc6cf0a96a627e678ffc996a8f9d1416200d6c81";
    sha256 = "1srjikizp8ip4h42x7kr4qf00lxcp1l8zp6h0r1ddfdyw8gv9001";
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
  bcdb-llvm4 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_4;
  };
  bcdb-llvm5 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_5;
  };
  bcdb-llvm6 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_6;
  };
  bcdb-llvm7 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_7;
  };
  bcdb-llvm8 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_8;
  };
  bcdb-llvm9 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_9;
  };
  bcdb-llvm10 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_10;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  bcdb-clang = callPackage ./build.nix {
    inherit (llvmPackages_10) stdenv;
    llvm = llvmPackages_10.llvm;
  };

  bcdb = bcdb-llvm10;

  inherit (import nixpkgs_llvm4 {}) llvmPackages_4;
}
