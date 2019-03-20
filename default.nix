let
  default_nixpkgs = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs-channels";
    rev = "91b1806476b7f78042d195537bba03620a868e82";
    sha256 = "0ykfzzwarmaxrc7l7zg4fhvlmb154lwr5jvb9aj02482sky9ky4v";
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

  bcdb-clang = callPackage ./build.nix {
    inherit (llvmPackages_7) stdenv;
    llvm = llvmPackages_7.llvm;
  };

  bcdb = bcdb-llvm7;
}
