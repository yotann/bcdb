{ pkgs ? import nix/import-flake-lock.nix
}:

let
  debugLLVM = llvm: llvm.overrideAttrs (o: {
    cmakeFlags = o.cmakeFlags ++ [
      "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
      "-DLLVM_ENABLE_ASSERTIONS=ON"
      "-DLLVM_BUILD_TESTS=OFF"
      "-DLLVM_BUILD_LLVM_DYLIB=ON"
      "-DLLVM_LINK_LLVM_DYLIB=ON"
    ];
    postInstall = builtins.replaceStrings
      ["LLVMExports-release"]
      ["LLVMExports-relwithdebinfo"]
      o.postInstall;
    doCheck = false;
    dontStrip = true;
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
  bcdb-llvm13 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = debugLLVM pkgs.llvmPackages_13.libllvm;
    clang = pkgs.llvmPackages_13.clang;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  # Also use ASAN and UBSAN to catch leaks and undefined behavior.
  bcdb-clang-sanitize = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    inherit (pkgs.llvmPackages_13) stdenv llvm clang;
    sanitize = true;
  };

  bcdb = bcdb-llvm13;

  bcdb-without-optional-deps = bcdb.override { nng = null; rocksdb = null; };

  symphony = pkgs.callPackage ./nix/symphony { inherit coinutils cgl; };
}
