{ stdenv, nix-gitignore, clang, cmake, libsodium, llvm, pkgconfig, python2, sqlite, xxd }:

let
  gitFilter = patterns: root: with nix-gitignore;
      gitignoreFilterPure (_: _: true) (withGitignoreFile patterns root) root;

in stdenv.mkDerivation {
  name = "bcdb";
  version = "0.0.1";

  src = builtins.path {
    path = ./.;
    name = "bcdb-source";
    filter = gitFilter [''
      .*
      *.nix
      /nix/
    ''] ./.;
  };

  nativeBuildInputs = [ clang cmake pkgconfig python2 xxd ];
  buildInputs = [ libsodium llvm sqlite ];

  cmakeBuildType = "Debug";
  doCheck = true;

  enableParallelBuilding = true;
}
