{ pkgs ? import nix/import-flake-lock.nix
}:

let
  tensorflow = with pkgs.python39Packages; callPackage ./nix/tensorflow {
    # from nixpkgs/pkgs/top-level/python-packages.nix
    inherit (pkgs.darwin.apple_sdk.frameworks) Foundation Security;
    flatbuffers-core = pkgs.flatbuffers;
    flatbuffers-python = flatbuffers;
    lmdb-core = pkgs.lmdb;

    # required for LLVM
    xlaSupport = true;
  };

  mlLLVMModel = pkgs.stdenv.mkDerivation {
    name = "ml-compiler-opt-inlining-Oz";
    src = pkgs.fetchurl {
      # see llvm/lib/Analysis/CMakeLists.txt
      url = "https://github.com/google/ml-compiler-opt/releases/download/inlining-Oz-v1.1/inlining-Oz-99f0063-v1.1.tar.gz";
      sha256 = "1lkcj1p86q0ip9292q4m5g1xl936ljzazislq7wxd0j35jz5z3ih";
    };
    installPhase = ''
      mkdir "$out"
      mv -i * "$out"/
    '';
  };

  # Enable TensorFlow (currently used for inlining in -Oz).
  mlLLVM = llvm: llvm.overrideAttrs (o: {
    cmakeFlags = o.cmakeFlags ++ [
      "-DTENSORFLOW_C_LIB_PATH=${tensorflow.libtensorflow}"
      "-DTF_PROTO_HEADERS=${tensorflow}/lib/python3.9/site-packages/tensorflow/include"
      "-DTENSORFLOW_AOT_PATH=${tensorflow}/lib/python3.9/site-packages/tensorflow"
      "-DLLVM_INLINER_MODEL_PATH=${mlLLVMModel}"
    ];
    # Our version of tensorflow uses the modern ABI.
    postPatch = o.postPatch + ''
      substituteInPlace CMakeLists.txt \
        --replace "-D_GLIBCXX_USE_CXX11_ABI=0" "-D_GLIBCXX_USE_CXX11_ABI=1"
    '';
    postInstall = o.postInstall + ''
      moveToOutput "lib/libtf_xla_runtime.*" "$lib"
    '';
  });

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
  llvm13-assert = assertLLVM (mlLLVM pkgs.llvmPackages_13.libllvm);
  llvm13-debug = debugLLVM (mlLLVM pkgs.llvmPackages_13.libllvm);
  nng = pkgs.callPackage ./nix/nng {};

  inherit tensorflow mlLLVMModel;

  sllim = pkgs.callPackage ./experiments/sllim {
    inherit bcdb;
    llvm = llvm13-assert;
  };

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
