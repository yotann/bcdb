let
  default_nixpkgs = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs-channels";
    rev = "a70d3bab72ddcc80d45d1150cdaf4e857ff2af0b";
    sha256 = "04l7jxlv8sp9n61b6irizbpjj8x4c70lpvbn5f2xyh29c5fiqqcz";
  };
in
{ nixpkgs ? default_nixpkgs }:

with import nixpkgs {};
{
  bcdb4 = callPackage ./build.nix {
    llvm = llvmPackages_4.llvm.override { debugVersion = true; };
  };
  bcdb5 = callPackage ./build.nix {
    llvm = llvmPackages_5.llvm.override { debugVersion = true; };
  };
  bcdb6 = callPackage ./build.nix {
    llvm = llvmPackages_6.llvm.override { debugVersion = true; };
  };
  bcdb7 = callPackage ./build.nix {
    llvm = llvmPackages_7.llvm.override { debugVersion = true; };
  };
  bcdb7-clang7 = callPackage ./build.nix {
    inherit (llvmPackages_7) stdenv;
    llvm = llvmPackages_7.llvm.override { debugVersion = true; };
  };
}
