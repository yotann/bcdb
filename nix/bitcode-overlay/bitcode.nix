# Overlay that provides pkgsBitcode, built with -fembed-bitcode.

# Not all packages will build but support is done on a
# best effort basis.

self: super: let
  original = self;
in {
  bitcodeWrapper = self.callPackage ../bitcode-cc-wrapper {
    inherit (self.llvmPackages_10) clang llvm bintools;
  };

  bitcodeStdenv = let
    stdenv' = self.stdenv.override {
      allowedRequisites = null;
      cc = self.bitcodeWrapper;
    };
  in stdenv' // {
    mkDerivation = args: stdenv'.mkDerivation (args // {
      # Not compatible with -fembed-bitcode.
      separateDebugInfo = false;
    });
  };

  pkgsBitcode = self.extend (

self: super: let
  inherit (super) lib;



  ### HELPER FUNCTIONS

  # When building with Clang, "fortify" causes snprintf etc. to be defined as
  # macros, which breaks various things. (Things don't break with GCC because
  # "fortify" is implemented differently in that case.)
  noFortify = pkg: pkg.overrideAttrs (o: {
    hardeningDisable = (o.hardeningDisable or []) ++ [ "fortify" ];
  });

  addCflags = cflags: pkg: pkg.overrideAttrs (o: {
    NIX_CFLAGS_COMPILE = (if o ? NIX_CFLAGS_COMPILE then o.NIX_CFLAGS_COMPILE + " " else "") + cflags;
  });

  # Tests fail. Probably because we're building in Singularity containers.
  noCheck = pkg: pkg.overrideAttrs (o: {
    doCheck = false;
    doInstallCheck = false;
  });

  fixIcu = icu: icu.overrideAttrs (o: {
    # Don't use assembly code for data tables.
    preConfigure = o.preConfigure + ''
      sed -i -e 's|withoutAssembly = FALSE|withoutAssembly = TRUE|' tools/pkgdata/pkgdata.cpp
      sed -i -e '1i#define USE_SINGLE_CCODE_FILE' tools/toolutil/pkg_genc.h
    '';
  });

  fixOldBoost = boost: boost.overrideAttrs (o: {
    postPatch = (o.postPatch or "") + ''
      sed -i -e 's/\bpth\b/pch/g' tools/build/*/tools/clang-linux.jam
    '';
  });

  fixLLVM = { shared-libs ? true, dylib ? false }: package: let

    base = package.override {
      buildLlvmTools = llvmPackages.tools;
      targetLlvmLibraries = llvmPackages.libraries;
    };

    tools = base.tools.extend (self: super: {

      llvm = super.llvm.overrideAttrs (o: {

        # Remove tests that fail for some reason.
        # eh.ll fails in LLVM 5-6, and parallel.ll fails in LLVM 5-10.
        postPatch = o.postPatch + ''
          rm test/ExecutionEngine/MCJIT/remote/eh.ll
          rm test/ExecutionEngine/OrcMCJIT/remote/eh.ll
          rm test/tools/gold/X86/parallel.ll
        '';

        # Control whether to build LLVM using many shared libraries
        # (shared-libs), a single shared library (dylib), or full static
        # linking (!shared-libs && !dylib).
        # See https://llvm.org/docs/CMake.html#llvm-specific-variables
        cmakeFlags = o.cmakeFlags ++ [
          "-DBUILD_SHARED_LIBS=${if shared-libs then "ON" else "OFF"}"
          "-DLLVM_BUILD_LLVM_DYLIB=${if dylib then "ON" else "OFF"}"
          "-DLLVM_LINK_LLVM_DYLIB=${if dylib then "ON" else "OFF"}"
        ];

        # Move the extra libraries to the "lib" output.
        postInstall = o.postInstall + ''
          moveToOutput "lib/lib*.a" "$lib"
          moveToOutput "lib/lib*.so*" "$lib"
          substituteInPlace "$out"/lib/cmake/llvm/LLVMExports-*.cmake \
            --replace "\''${_IMPORT_PREFIX}/lib/lib" "$lib/lib/lib"
        '';

      });

      # Same changes as LLVM to handle shared-libs and dylib.
      clang-unwrapped = super.clang-unwrapped.overrideAttrs (o: {
        cmakeFlags = o.cmakeFlags ++ [
          "-DBUILD_SHARED_LIBS=${if shared-libs then "ON" else "OFF"}"
          "-DCLANG_LINK_CLANG_DYLIB=${if dylib then "ON" else "OFF"}"
        ];
        postPatch = o.postPatch + lib.optionalString (!dylib) ''
          # Don't build the combined library libclang-cpp.so.
          if [ -e tools/clang-shlib ]; then
            :>tools/clang-shlib/CMakeLists.txt
          fi
        '';
        postInstall = o.postInstall + ''
          moveToOutput "lib/lib*.so*" "$lib"
        '';
      });
    });

    libraries = base.libraries.extend (self: super: {
      compiler-rt = noFortify super.compiler-rt;
    });

    llvmPackages = { inherit tools libraries; } // libraries // tools;
  in llvmPackages;

in {



  ### OVERRIDE STDENV

  # This is the part that actually causes -fembed-bitcode to be used!

  stdenv = original.bitcodeStdenv;
  clangStdenv = original.bitcodeStdenv;
  libcxxStdenv = super.libcxxStdenv;



  ### FIXES

  # Various packages need help to build with -fembed-bitcode, for several
  # reasons:
  #
  # - They try to run GCC, but we need them to use Clang instead.
  # - They use -Werror, and Clang produces warnings when GCC doesn't.
  # - They use other compiler flags supported by GCC but not Clang.
  # - They use compiler flags incompatible with -fembed-bitcode.
  # - Their tests fail on my build farm (which uses Singularity containers).
  # - Miscellaneous reasons.
  #
  # Most of the compiler flag issues are fixed by
  # pkgs/build-support/bitcode-wrapper/wrapper.sh, but the other problems are
  # resolved here on a package-by-package basis.

  # fatal error: 'strstream' file not found
  inherit (original) asio asio_1_10 asio_1_12;

  audit = super.audit.overrideAttrs (o: {
    # Prevent using GCC to build.
    depsBuildBuild = [];
  });

  # Prevent linking libssp (which conflicts with -fPIC for some reason).
  avahi = super.avahi.overrideAttrs (o: {
    configureFlags = o.configureFlags ++ [ "--disable-stack-protector" ];
  });
  avahi-compat = super.avahi-compat.overrideAttrs (o: {
    configureFlags = o.configureFlags ++ [ "--disable-stack-protector" ];
  });

  bash-completion = noCheck super.bash-completion;

  boost155 = fixOldBoost super.boost155;
  boost159 = fixOldBoost super.boost159;
  boost160 = fixOldBoost super.boost160;
  boost165 = fixOldBoost super.boost165;
  boost166 = fixOldBoost super.boost166;
  boost167 = fixOldBoost super.boost167;
  boost168 = fixOldBoost super.boost168;

  cmake = super.cmake.overrideAttrs (o: {
    # Prevent using GCC to build.
    depsBuildBuild = [];
  });

  # Test tests/df/df-symlink.sh fails on my build farm.
  coreutils = noCheck super.coreutils;

  cyrus_sasl = noFortify super.cyrus_sasl;

  dmraid = addCflags "-Wno-error=return-type" super.dmraid;

  # Requires GCC.
  inherit (original) glibc;

  # Incompatible assembler syntax.
  inherit (original) gmp;

  # Test gnutls-cli-rawpk.sh fails on my build farm.
  gnutls = noCheck super.gnutls;

  icu58 = fixIcu super.icu58;
  icu59 = fixIcu super.icu59;
  icu60 = fixIcu super.icu60;
  icu63 = fixIcu super.icu63;
  icu64 = fixIcu super.icu64;

  libmemcached = super.libmemcached.overrideAttrs (o: {
    # Fix error comparing char* with bool.
    postPatch = (o.postPatch or "") + ''
      substituteInPlace clients/memflush.cc --replace "false" "0"
    '';
  });

  libnftnl = noFortify super.libnftnl;

  liboil = addCflags "-fheinous-gnu-extensions" super.liboil;

  libselinux = super.libselinux.overrideAttrs (o: {
    # src/exception.sh requires gcc to use -aux-info
    buildInputs = o.buildInputs ++ [ original.gcc ];
  });

  libuv = noCheck super.libuv;

  lighttpd = noCheck super.lighttpd;

  llvmPackages_5 = fixLLVM {} super.llvmPackages_5;
  llvmPackages_6 = fixLLVM {} super.llvmPackages_6;
  llvmPackages_7 = fixLLVM {} super.llvmPackages_7;
  llvmPackages_8 = fixLLVM {} super.llvmPackages_8;
  llvmPackages_9 = fixLLVM {} super.llvmPackages_9;
  llvmPackages_10 = fixLLVM {} super.llvmPackages_10;
  llvmPackages_10_dylib = fixLLVM { shared-libs = false; dylib = true; } super.llvmPackages_10;
  clang_10_dylib = self.llvmPackages_10_dylib.clang;
  llvm_10_dylib = self.llvmPackages_10_dylib.llvm;

  mariadb = let
    client = super.mariadb.client;
    # storage/tokudb/PerconaFT/CMakeLists.txt is confused by warning messages
    server = (addCflags "-w" super.mariadb.server).overrideAttrs (o: {
      # https://github.com/facebook/rocksdb/pull/6161
      postPatch = (o.postPatch or "") + ''
        substituteInPlace storage/rocksdb/rocksdb/util/channel.h \
          --replace "std::mutex lock_" "mutable std::mutex lock_"
      '';
    });
  in server // { inherit client server; };

  mesa = super.mesa.overrideAttrs (o: {
    # -mstackrealign is incompatible with -fembed-bitcode
    postPatch = (o.postPatch or "") + ''
      for fn in ./meson.build scons/gallium.py src/intel/meson.build; do
        substituteInPlace $fn --replace "-mstackrealign" ""
      done
    '';
  });

  nss = super.nss.overrideAttrs (o: {
    # Prevent using GCC to build.
    depsBuildBuild = [];
  });

  # Ensure perl.buildPerl = perl.perl. Necessary for packages that build .so
  # modules and run tests using buildPerl, because modules built with perl
  # built with Clang require the symbol perl_tsa_mutex_lock to be present in
  # libperl.so, but it's absent from perl built with GCC.
  perlInterpreters = builtins.mapAttrs (name: value:
    value.override { buildPackages = self; }
  ) super.perlInterpreters;

  # floating point exceptions
  pixman = noCheck super.pixman;

  pythonInterpreters = builtins.mapAttrs (name: value:
  let python = (value.override {
      # llvm-profdata is required for a --enable-optimizations build but could not be found.
      #enableOptimizations = false;

      packageOverrides = self: super: {
        # Some tests fail because we disable -Werror
        # (and possibly also because they assume GCC):
        # testing/cffi0/test_verify.py
        # testing/cffi1/test_verify1.py
        cffi = noCheck super.cffi;

        # tests fail on my build farm
        coloredlogs = noCheck super.coloredlogs;
        dulwich = noCheck super.dulwich;
        execnet = noCheck super.execnet;
        imgaug = noCheck super.imgaug;
        lmdb = noCheck super.lmdb;
        paramiko = noCheck super.paramiko;
        pytest-localserver = noCheck super.pytest-localserver;
        python-gnupg = noCheck super.python-gnupg;
        tornado = noCheck super.tornado;
      };
    }).overrideAttrs (o: {
      # python adds openssl-dev to this, which breaks WLLVM build.
      disallowedReferences = null;
    } // lib.optionalAttrs (value.sourceVersion.major == "3") {
      # Python 3 creates an empty libpython3.so whose sole purpose is to link
      # in libpython3.*m.so.1.0. The empty ELF file confuses the bitcode
      # extraction tool, so as a workaround we add an empty C file to it.
      postBuild = (o.postBuild or "") + ''
        : >empty.c
        clang -shared -Wl,--no-as-needed -o libpython3.so -Wl,-hlibpython3.so libpython3.*m.so.* empty.c
      '';
    });
    in python // { pythonForBuild = python; }
  ) super.pythonInterpreters;

  systemd = super.systemd.overrideAttrs (o: {
    # src/boot/efi uses -mno-red-zone, which breaks -fembed-bitcode.
    mesonFlags = o.mesonFlags ++ ["-Dgnu-efi=false"];
    # Remove stray references that are left in libsystemd.so because we disable
    # -Wl,--gc-sections.
    postFixup = o.postFixup + ''
      nukedRef=$(echo $out | sed -e "s,$NIX_STORE/[^-]*-\(.*\),$NIX_STORE/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee-\1,")
      cat $lib/lib/libsystemd.so | perl -pe "s|$out|$nukedRef|" > $lib/lib/libsystemd.so.tmp
      mv $lib/lib/libsystemd.so.tmp $(readlink -f $lib/lib/libsystemd.so)
    '';
  });

  # Prevent using GCC to build.
  texinfo413 = super.texinfo413.overrideAttrs (o: { depsBuildBuild = []; });
  texinfo5 = super.texinfo5.overrideAttrs (o: { depsBuildBuild = []; });
  texinfo6 = super.texinfo6.overrideAttrs (o: { depsBuildBuild = []; });

  # Prevent using GCC to build.
  tzdata = super.tzdata.overrideAttrs (o: { depsBuildBuild = []; });

  # Segfault test fails on my build farm.
  udisks2 = noCheck super.udisks2;



  ### BROKEN PACKAGES

  # These packages are used as dependencies of other packages, but they don't
  # build correctly with -fembed-bitcode and I haven't figured out how to fix
  # them yet. So we use the original, non-bitcode versions.

  inherit (original) glibcLocales;

  inherit (original) rhash;

  # unrecognized compiler flag -maccumulate-outgoing-args
  inherit (original) gnu-efi;

  # undefined reference to `__muloti4' from xmalloc.c
  inherit (original) cpio gnum4;

  # requires GCC for nested functions
  inherit (original) elfutils;

  # segfault in libguile-2.0.so
  inherit (original) guile_2_0;

  # unknown assembler directive .arch
  inherit (original) kexectools;

  # undeclared identifier 'LONG_MIN' in libiberty/fibheap.c
  inherit (original) gdb libiberty;

  # tries to run gcc
  inherit (original) libtermkey libvterm-neovim nx-libs syslinux;

  # can't find <omp.h>
  inherit (original) openblas openblasCompat;

  # Running unit tests: free(): invalid pointer
  inherit (original) unittest-cpp;

  # missing <cstdio>
  inherit (original) vte;
});
}
