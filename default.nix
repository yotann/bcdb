# NOTE: when updating this file, run utils/cache-deps.sh to upload dependencies
# to Cachix so CI builds can use them.

let
  default_nixpkgs = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs-channels";
    rev = "91d5b3f07d27622ff620ff31fa5edce15a5822fa";
    sha256 = "09vlhjbkjivv9aiklhwq9wpzn954pcyp3fhrwslm28ip4iar9b55";
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

  bcdb-clang = callPackage ./build.nix {
    inherit (llvmPackages_7) stdenv;
    llvm = llvmPackages_7.llvm;
  };

  bcdb = bcdb-llvm7;
}
