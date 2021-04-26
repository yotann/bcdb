{ pkgs ? import nix/import-flake-lock.nix
}:

let
  debugLLVM = llvm: (llvm.override {
    debugVersion = true;
  }).overrideAttrs (o: {
    doCheck = false;
    # TODO: also prevent building test files
  });

  llvm12 = pkgs.llvmPackages_11.llvm.overrideAttrs (o: {
    src = pkgs.fetchFromGitHub {
      # LLVM 12.0.0-rc1
      owner = "llvm";
      repo = "llvm-project";
      rev = "8364f5369eeeb2da8db2bae7716c549930d8df93";
      sha256 = "1ypicjlxxmn7svzi8s6h4jfwf1qzafs0792q5wmgkz2w5qmahy2w";
    };
    unpackPhase = null;
    sourceRoot = "source/llvm";
  });

  # Alive2 needs a special build of LLVM main.
  llvmAlive = llvm12.overrideAttrs (o: {
    cmakeFlags = o.cmakeFlags ++ [
      "-DLLVM_ENABLE_RTTI=ON"
      "-DLLVM_ENABLE_EH=ON"
    ];
  });

  nng = pkgs.callPackage ./nix/nng {};
  nngpp = pkgs.callPackage ./nix/nngpp { inherit nng; };

in rec {
  bcdb-llvm7 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp;
    llvm = debugLLVM pkgs.llvmPackages_7.llvm;
    clang = pkgs.clang_7;
  };
  bcdb-llvm8 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp;
    llvm = debugLLVM pkgs.llvmPackages_8.llvm;
    clang = pkgs.clang_8;
  };
  bcdb-llvm9 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp;
    llvm = debugLLVM pkgs.llvmPackages_9.llvm;
    clang = pkgs.clang_9;
  };
  bcdb-llvm10 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp;
    llvm = debugLLVM pkgs.llvmPackages_10.llvm;
    clang = pkgs.clang_10;
  };
  bcdb-llvm11 = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp;
    llvm = debugLLVM pkgs.llvmPackages_11.llvm;
    clang = pkgs.clang_11;
  };
  bcdb-llvmAlive = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp;
    llvm = debugLLVM llvmAlive;
    clang = pkgs.clang_11;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  bcdb-clang = pkgs.callPackage ./nix/bcdb {
    inherit nng nngpp;
    inherit (pkgs.llvmPackages_11) stdenv llvm clang;
  };

  bcdb = bcdb-llvm11;
}
