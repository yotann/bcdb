{ pkgs ? import nix/import-flake-lock.nix
}:

let
  debugLLVM = llvm: (llvm.override {
    debugVersion = true;
  }).overrideAttrs (o: {
    doCheck = false;
    # TODO: also prevent building test files
  });

  nng = pkgs.callPackage ./nix/nng {};

  coinutils = pkgs.callPackage ./nix/coinutils {};
  cgl = pkgs.callPackage ./nix/cgl { inherit coinutils; };

in rec {
  bcdb-llvm10 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = debugLLVM pkgs.llvmPackages_10.libllvm;
    clang = pkgs.clang_10;
  };
  bcdb-llvm11 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = debugLLVM pkgs.llvmPackages_11.libllvm;
    clang = pkgs.clang_11;
  };
  bcdb-llvm12 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = debugLLVM pkgs.llvmPackages_12.libllvm;
    clang = pkgs.clang_12;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  # Also use ASAN and UBSAN to catch leaks and undefined behavior.
  bcdb-clang-sanitize = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    inherit (pkgs.llvmPackages_12) stdenv llvm clang;
    sanitize = true;
  };

  bcdb = bcdb-llvm12;

  bcdb-without-optional-deps = bcdb.override { nng = null; rocksdb = null; };

  symphony = pkgs.callPackage ./nix/symphony { inherit coinutils cgl; };
}
