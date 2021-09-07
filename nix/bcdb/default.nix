{ stdenv, lib, nix-gitignore, clang, cmake, libsodium, llvm, python3, sqlite,
rocksdb ? null, nng ? null,
sanitize ? false }:

let
  gitFilter = patterns: root: with nix-gitignore;
      gitignoreFilterPure (_: _: true) (withGitignoreFile patterns root) root;

  # In order to determine REVISION_DESCRIPTION_FINAL the normal way, we would
  # have to copy all of .git/ into the Nix store, which is very slow. Instead,
  # we can determine the current revision using Nix.
  symref = lib.removePrefix "ref: " (lib.fileContents ../../.git/HEAD);
  revision = lib.fileContents (../../.git + ("/" + symref));
  revision-short = builtins.substring 0 7 revision;

in stdenv.mkDerivation {
  name = "bcdb";
  version = "0.1.0-${revision-short}";

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

  nativeBuildInputs = [ clang cmake python3 ];
  buildInputs = [ libsodium llvm nng rocksdb sqlite ];

  preConfigure = ''
    patchShebangs third_party/lit/lit.py
  '';
  cmakeBuildType = "RelWithDebInfo";
  doCheck = true;
  dontStrip = true;

  enableParallelBuilding = true;

  cmakeFlags = [
    "-DREVISION_DESCRIPTION=g${revision-short}-NIX"
  ] ++ lib.optional sanitize "-DCMAKE_BUILD_TYPE=SANITIZE";
  preCheck = lib.optionalString sanitize
    "export LSAN_OPTIONS=suppressions=$src/utils/lsan.supp";
}
