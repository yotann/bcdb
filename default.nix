{ pkgs ? import nix/import-flake-lock.nix
}:

let
  debugLLVM = llvm: (llvm.override {
    debugVersion = true;
  }).overrideAttrs (o: {
    doCheck = false;
    # TODO: also prevent building test files
  });

  # Alive2 needs a special build of LLVM main.
  llvmAlive = pkgs.llvmPackages_12.libllvm.overrideAttrs (o: {
    cmakeFlags = o.cmakeFlags ++ [
      "-DLLVM_ENABLE_RTTI=ON"
      "-DLLVM_ENABLE_EH=ON"
    ];
  });

  nng = pkgs.callPackage ./nix/nng {};

  coinutils = pkgs.callPackage ./nix/coinutils {};
  cgl = pkgs.callPackage ./nix/cgl { inherit coinutils; };

in rec {
  bcdb-llvm9 = pkgs.callPackage ./nix/bcdb {
    inherit nng rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_9.libllvm;
    clang = pkgs.clang_9;
  };
  bcdb-llvm10 = pkgs.callPackage ./nix/bcdb {
    inherit nng rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_10.libllvm;
    clang = pkgs.clang_10;
  };
  bcdb-llvm11 = pkgs.callPackage ./nix/bcdb {
    inherit nng rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_11.libllvm;
    clang = pkgs.clang_11;
  };
  bcdb-llvm12 = pkgs.callPackage ./nix/bcdb {
    inherit nng rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_12.libllvm;
    clang = pkgs.clang_12;
  };
  bcdb-llvmAlive = pkgs.callPackage ./nix/bcdb {
    inherit nng rocksdb;
    llvm = debugLLVM llvmAlive;
    clang = pkgs.clang_12;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  # Also use ASAN and UBSAN to catch leaks and undefined behavior.
  bcdb-clang-sanitize = pkgs.callPackage ./nix/bcdb {
    inherit nng rocksdb;
    inherit (pkgs.llvmPackages_12) stdenv llvm clang;
    sanitize = true;
  };

  bcdb = bcdb-llvm12;

  bcdb-sqlite-only = bcdb.override { rocksdb = null; };
  bcdb-without-nng = bcdb.override { nng = null; };

  rocksdb = pkgs.rocksdb.overrideAttrs (o: {
    version = "6.20.3";
    src = pkgs.fetchFromGitHub {
      owner = "facebook";
      repo = "rocksdb";
      rev = "v6.20.3";
      sha256 = "106psd0ap38d0b5ghm6gy66ig02xn8yjmzpb8l6x23kvw7vzrfrc";
    };
    # Install the tools.
    postInstall = ''
      mkdir -p $out/bin
      cp tools/ldb tools/sst_dump $out/bin/
    '';
  });

  symphony = pkgs.callPackage ./nix/symphony { inherit coinutils cgl; };
}
