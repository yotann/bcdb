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
    inherit (llvmPackages_4) llvm;
  };
  bcdb5 = callPackage ./build.nix {
    inherit (llvmPackages_5) llvm;
  };
  bcdb6 = callPackage ./build.nix {
    inherit (llvmPackages_6) llvm;
  };
  bcdb7 = callPackage ./build.nix {
    inherit (llvmPackages_7) llvm;
  };
  bcdb7-clang7 = callPackage ./build.nix {
    inherit (llvmPackages_7) llvm stdenv;
  };
}
