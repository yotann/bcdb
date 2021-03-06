{ stdenv, nix-gitignore, clang, cmake, libsodium, llvm, pkgconfig, python2, sqlite, xxd }:

let
  gitFilter = patterns: root: with nix-gitignore;
      gitignoreFilterPure (_: _: true) (withGitignoreFile patterns root) root;

in stdenv.mkDerivation {
  name = "bcdb";
  version = "0.0.1";

  src = builtins.path {
    path = ../..;
    name = "bcdb-source";
    filter = gitFilter [''
      .*
      *.md
      *.nix
      /docs/
      /flake.lock
      /nix/
      /third_party/pyperformance/
      /third_party/sqlite/
    ''] ../..;
  };

  nativeBuildInputs = [ clang cmake pkgconfig python2 xxd ];
  buildInputs = [ libsodium llvm sqlite ];

  preConfigure = ''
    patchShebangs third_party/lit/lit.py
  '';
  cmakeBuildType = "Debug";
  doCheck = true;
  dontStrip = true;

  enableParallelBuilding = true;
}
