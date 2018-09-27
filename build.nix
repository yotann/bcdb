{ stdenv, cmake, llvm, python2 }:

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

  nativeBuildInputs = [ cmake python2 ];
  buildInputs = [ llvm ];

  doCheck = true;

  enableParallelBuilding = true;
}
