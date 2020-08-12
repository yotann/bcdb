{ pkgs ? import (builtins.fetchTarball ((builtins.fromJSON (builtins.readFile ./flake.lock)).nodes.nixpkgs.locked.url)) {}
}:

let
  debugLLVM = llvm: (llvm.override {
    debugVersion = true;
  }).overrideAttrs (o: {
    doCheck = false;
  });

in rec {
  bcdb-llvm6 = pkgs.callPackage ./nix/bcdb {
    llvm = debugLLVM pkgs.llvmPackages_6.llvm;
    clang = pkgs.clang_6;
  };
  bcdb-llvm7 = pkgs.callPackage ./nix/bcdb {
    llvm = debugLLVM pkgs.llvmPackages_7.llvm;
    clang = pkgs.clang_7;
  };
  bcdb-llvm8 = pkgs.callPackage ./nix/bcdb {
    llvm = debugLLVM pkgs.llvmPackages_8.llvm;
    clang = pkgs.clang_8;
  };
  bcdb-llvm9 = pkgs.callPackage ./nix/bcdb {
    llvm = debugLLVM pkgs.llvmPackages_9.llvm;
    clang = pkgs.clang_9;
  };
  bcdb-llvm10 = pkgs.callPackage ./nix/bcdb {
    llvm = debugLLVM pkgs.llvmPackages_10.llvm;
    clang = pkgs.clang_10;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  bcdb-clang = pkgs.callPackage ./nix/bcdb {
    inherit (pkgs.llvmPackages_10) stdenv llvm clang;
  };

  bcdb = bcdb-llvm10;
}
