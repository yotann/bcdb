{ stdenv, lib, nix-gitignore }:

let
  gitFilter = patterns: root: with nix-gitignore;
      gitignoreFilterPure (_: _: true) (withGitignoreFile patterns root) root;

in stdenv.mkDerivation {
  name = "sllim";
  version = "0.0.1";

  src = builtins.path {
    path = ./.;
    name = "sllim-source";
    filter = gitFilter [''
      *.nix
    ''] ./.;
  };

  installPhase = ''
    mkdir -p "$out"
    mv bin "$out"
    mv libexec "$out"
  '';
}
