{ pkgs, pkgsBitcode }:

{
  # Basic C test
  hello = {
    packages = with pkgsBitcode; [ hello ];
  };

  boost1 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    # Exclude icu library because we want to focus on reducing the size of Boost.
    # XXX: results in the paper did *not* exclude icu64, so the results were
    # different.
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost2 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost3 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost4 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost5 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost6 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost7 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost8 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 boost169 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost9 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 boost169 boost170 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost10 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 boost169 boost170 boost171 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost11 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 boost169 boost170 boost171 boost172 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost12 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 boost169 boost170 boost171 boost172 boost173 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost13 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 boost169 boost170 boost171 boost172 boost173 boost174 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  boost14 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ boost155 boost159 boost160 boost165 boost166 boost167 boost168 boost169 boost170 boost171 boost172 boost173 boost174 boost175 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
    exclude = [ pkgsBitcode.icu64 ];
  };

  protobuf1 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf2 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf3 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_6 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf4 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_6 protobuf3_7 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf5 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_6 protobuf3_7 protobuf3_8 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf6 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf7 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_10 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf8 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_10 protobuf3_11 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf9 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_10 protobuf3_11 protobuf3_12 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf10 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_10 protobuf3_11 protobuf3_12 protobuf3_13 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf11 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_10 protobuf3_11 protobuf3_12 protobuf3_13 protobuf3_14 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf12 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_10 protobuf3_11 protobuf3_12 protobuf3_13 protobuf3_14 protobuf3_15 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  protobuf13 = {
    lto-flags = "-Oz";
    packages = with pkgsBitcode; [ protobuf2_5 protobuf3_1 protobuf3_10 protobuf3_11 protobuf3_12 protobuf3_13 protobuf3_14 protobuf3_15 protobuf3_16 protobuf3_6 protobuf3_7 protobuf3_8 protobuf3_9 ];
    configurations = {
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };

  python = {
    packages = [
      # Build Python with extra modules needed for the benchmarks.
      (pkgsBitcode.python3.withPackages (ps: [
        ps.pyperf
        ps.chameleon
        ps.django
        ps.genshi
        ps.Mako
        ps.sqlalchemy
        ps.pyaes
        ps.sympy
        ps.tornado
        ps.psutil
        ps.numpy
        ps.setuptools
        # Disable checks for dulwich, because they bring in extra packages.
        (ps.dulwich.overrideAttrs (o: { doInstallCheck = false; } ))
      ]))
    ];

    exclude = [
      # These packages use assembly code that can't be compiled to IR, so we
      # don't include them in the optimized set.
      pkgsBitcode.libffi
      pkgsBitcode.openssl.out
    ];

    configurations.default = {
      nooverride = true;
      nouse = true;
      noplugin = true;
      noweak = true;
    };

    symbol-list = ''
      # Each module's PyInit_<modulename> function is loaded with dlsym().
      [gl-use]
      fun:PyInit_*

      # Python modules are loaded with dlopen().
      [gl-plugin]
      lib:*/lib-dynload/*
      lib:*/site-packages/*

      # These libraries may be loaded as dependencies of Python modules.
      [gl-plugin]
      lib:*libexpat.so*
      lib:*libgdbm_compat.so*
      lib:*libgdbm.so*
      lib:*libpanelw.so*
      lib:*libreadline.so*
      lib:*libsqlite3.so*
    '';

    lto-flags = "-O3";

    profile-commands = ''
      export PATH=$PWD/bin:$PATH
      export PYTHONHOME=$PWD
      export PYTHONPATH=$PWD/lib/python*/site-packages
      cat ${./python-benchmarks.txt} | while read BENCH; do
        BENCH=''${BENCH#* }
        bin/python3 \
          ${third_party/pyperformance/pyperformance/benchmarks}/$BENCH \
          --no-locale \
          --processes 1 \
          --values 1 \
          --inherit-environ PYTHONHOME,PYTHONPATH,LLVM_PROFILE_FILE
      done
    '';
  };

  clang = {
    lto-flags = "-O3";
    profile-commands = ''
      bin/clang -fPIC -shared -O3 ${./third_party/sqlite/sqlite-amalgamation-3320000.c} -ldl -lpthread -o /dev/null \
        ${(import ./clang-cmdline.nix { exepath = "."; }).args}
    '';
    packages = with pkgsBitcode.llvmPackages_12; [ bintools clang clang-unwrapped llvm ];
    exclude = with pkgsBitcode; [
      # needs assembly files
      libffi
      # overrides libc functions (such as __tls_get_addr and __fprintf_chk)
      llvmPackages_12.compiler-rt
      # might cause conflicts with -lstdc++
      llvmPackages_12.libcxx
      llvmPackages_12.libcxxabi
    ];
    configurations = {
      closed        = { noplugin = true; nooverride = true;  nouse = true;  noweak = true;  };
      open-spurious = { noplugin = true; nooverride = true;  nouse = false; noweak = true;  };
      open          = { noplugin = true; nooverride = true;  nouse = false; noweak = false; };
      interposable  = { noplugin = true; nooverride = false; nouse = false; noweak = false; };
    };
  };
}
