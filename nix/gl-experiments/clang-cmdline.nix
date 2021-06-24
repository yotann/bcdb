# Normally, Nixpkgs adds these options using a wrapper around Clang.
# We can't use the wrapper because it hardcodes the path to the original
# Clang executable, not our optimized version.

{ exepath }:
let
    pkgs = import ../bitcode-overlay;
in {
  args = pkgs.lib.escapeShellArgs [
    "-B${pkgs.llvmPackages_12.clang-unwrapped.lib}"
    "-resource-dir=${pkgs.llvmPackages_12.clang}/resource-root"
    "--gcc-toolchain=${pkgs.gcc-unwrapped.out}"
    "-B${pkgs.glibc.out}/lib/"
    "-idirafter"
    "${pkgs.glibc.dev}/include"
    "-B${pkgs.glibc.dev}/include"
    "-B${exepath}/bin"
    "-L${pkgs.glibc.out}/lib"
    "-v"
  ];
}
