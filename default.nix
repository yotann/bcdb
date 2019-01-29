let
  default_nixpkgs = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs-channels";
    rev = "11cf7d6e1ffd5fbc09a51b76d668ad0858a772ed";
    sha256 = "0zcg4mgfdk3ryiqj1j5iv5bljjvsgi6q6j9z1vkq383c4g4clc72";
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

  bcdb-llvm7-clang7 = callPackage ./build.nix {
    inherit (llvmPackages_7) stdenv;
    llvm = debugLLVM llvmPackages_7;
  };

  bcdb = bcdb-llvm7;
}
