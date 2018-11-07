{ stdenv, cmake, libsodium, llvm, pkgconfig, python2, sqlite }:

let
  inherit (stdenv) lib;
  sourceFilter = name: type: let baseName = baseNameOf (toString name); in
    (lib.cleanSourceFilter name type) && !(
      (type == "directory" && (lib.hasPrefix "build" baseName ||
                               lib.hasPrefix "install" baseName))
  );
in

stdenv.mkDerivation {
  name = "bcdb";
  version = "0.0.1";

  src = builtins.filterSource sourceFilter ./.;

  nativeBuildInputs = [ cmake pkgconfig python2 ];
  buildInputs = [ libsodium llvm sqlite ];

  doCheck = true;

  enableParallelBuilding = true;
}
