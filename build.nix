{ stdenv, nix-gitignore, clang, cmake, libsodium, llvm, pkgconfig, python2, sqlite, xxd }:

stdenv.mkDerivation {
  name = "bcdb";
  version = "0.0.1";

  src = nix-gitignore.gitignoreSource [''
    .*
    *.nix
    /nix/
  ''] ./.;

  nativeBuildInputs = [ clang cmake pkgconfig python2 xxd ];
  buildInputs = [ libsodium llvm sqlite ];

  cmakeBuildType = "Debug";
  doCheck = true;

  enableParallelBuilding = true;
}
