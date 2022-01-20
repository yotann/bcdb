{ pkgs ? import nix/import-flake-lock.nix
}:

let
  assertLLVM = llvm: llvm.overrideAttrs (o: {
    cmakeFlags = o.cmakeFlags ++ [
      "-DLLVM_ENABLE_ASSERTIONS=ON"
      "-DLLVM_BUILD_TESTS=OFF"
    ];
    doCheck = false;
  });

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

in rec {
  # default BCDB versions
  bcdb = bcdb-llvm13;
  bcdb-debug = bcdb-llvm13debug;

  # BCDB with various versions of LLVM (assertions enabled)
  bcdb-llvm10 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = llvm10-assert;
    clang = pkgs.clang_10;
  };
  bcdb-llvm11 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = llvm11-assert;
    clang = pkgs.clang_11;
  };
  bcdb-llvm12 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = llvm12-assert;
    clang = pkgs.clang_12;
  };
  bcdb-llvm13 = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = llvm13-assert;
    clang = pkgs.llvmPackages_13.clang;
  };

  # BCDB and LLVM with debugging info, intended for local development
  bcdb-llvm13debug = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    llvm = llvm13-debug;
    clang = pkgs.llvmPackages_13.clang;
  };

  # Build with Clang instead of GCC (may produce different warnings/errors).
  # Also use ASAN and UBSAN to catch leaks and undefined behavior.
  bcdb-clang-sanitize = pkgs.callPackage ./nix/bcdb {
    inherit nng;
    inherit (pkgs.llvmPackages_13) stdenv llvm clang;
    sanitize = true;
  };

  # Test whether BCDB works without these optional libraries
  bcdb-without-optional-deps = bcdb.override { nng = null; rocksdb = null; };

  # Dependencies of BCDB
  llvm10-assert = assertLLVM pkgs.llvmPackages_10.libllvm;
  llvm11-assert = assertLLVM pkgs.llvmPackages_11.libllvm;
  llvm12-assert = assertLLVM pkgs.llvmPackages_12.libllvm;
  llvm13-assert = assertLLVM pkgs.llvmPackages_13.libllvm;
  llvm13-debug = debugLLVM pkgs.llvmPackages_13.libllvm;
  nng = pkgs.callPackage ./nix/nng {};

  sllim = pkgs.callPackage ./experiments/sllim {};

  # Singularity container (to be run on HTCondor cluster)
  smout-worker-singularity = pkgs.singularity-tools.buildImage {
    name = "smout-worker";
    contents = [ pkgs.busybox ];
    diskSize = 4096;
    runScript = ''
      #!/bin/sh
      set +e
      for i in $(seq 4); do
        while true; do
          echo starting worker...
          ${bcdb}/bin/smout worker "$@"
          echo exit code: $?
        done &
      done
      sleep 7d
      kill $(jobs -p)
    '';
  };
}
