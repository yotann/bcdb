{ stdenvNoCC, lib, clang, bash }:

# This package provides "clang" and "clang++" wrappers that add the
# -fembed-bitcode option, remove other options that conflict with
# -fembed-bitcode, and invoke the actual "clang" and "clang++" executables.

let
  stdenv = stdenvNoCC;
in stdenv.mkDerivation {
  name = "${lib.getName clang}-bitcode";
  version = lib.getVersion clang;
  shell = "${bash}/bin/bash";
  dontBuild = true;
  dontConfigure = true;
  unpackPhase = ''
    src=$PWD
  '';
  wrapper = ./wrapper.sh;
  installPhase = ''
    wrap() {
      export dst="$1"
      export prog="$2"
      substituteAll "$wrapper" "$out/bin/$dst"
      chmod +x "$out/bin/$dst"
    }
    mkdir -p "$out/bin"
    wrap clang "${clang}/bin/clang"
    wrap clang++ "${clang}/bin/clang++"
    ln -s clang "$out/bin/cc"
    ln -s clang++ "$out/bin/c++"
    ln -s "${clang}/bin/cpp" "$out/bin/cpp"

    # Many packages are hardcoded to use gcc.
    ln -s clang "$out/bin/gcc"
    ln -s clang++ "$out/bin/g++"
  '';
  strictDeps = true;
  propagatedBuildInputs = [
    # Ensure the cc-wrapper and bintools setup hooks are run.
    clang
  ];
  wrapperName = "BITCODE_WRAPPER";

  setupHooks = [
    ./setup-hook.sh
  ];

  targetPrefix = "";

  # Provide the same attributes as clang, so things like stdenv.cc.isClang
  # still work.
  passthru = {
    isClang = true;
    isGNU = false;
    inherit (clang) nativeTools nativeLibc nativePrefix noLibc libc libc_dev cc shell bintools;
  };
}
