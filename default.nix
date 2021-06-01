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
  nngpp = pkgs.callPackage ./nix/nngpp { inherit nng; };

  coinutils = pkgs.callPackage ./nix/coinutils {};
  cgl = pkgs.callPackage ./nix/cgl { inherit coinutils; };

in rec {
  bcdb-llvm7 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_7.libllvm;
    clang = pkgs.clang_7;
  };
  bcdb-llvm8 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_8.libllvm;
    clang = pkgs.clang_8;
  };
  bcdb-llvm9 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_9.libllvm;
    clang = pkgs.clang_9;
  };
  bcdb-llvm10 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_10.libllvm;
    clang = pkgs.clang_10;
  };
  bcdb-llvm11 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_11.libllvm;
    clang = pkgs.clang_11;
  };
  bcdb-llvm12 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    llvm = debugLLVM pkgs.llvmPackages_12.libllvm;
    clang = pkgs.clang_12;
  };
  bcdb-llvmAlive = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    llvm = debugLLVM llvmAlive;
    clang = pkgs.clang_12;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  bcdb-clang = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp rocksdb;
    inherit (pkgs.llvmPackages_12) stdenv llvm clang;
  };

  bcdb = bcdb-llvm12;

  bcdb-without-nng = bcdb.override { nng = null; nngpp = null; };

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
