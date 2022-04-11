{ stdenv, lib, nix-gitignore, python3Packages, bcdb, llvm }:

let
  gitFilter = patterns: root: with nix-gitignore;
      gitignoreFilterPure (_: _: true) (withGitignoreFile patterns root) root;

in python3Packages.buildPythonApplication {
  pname = "sllim";
  version = "0.0.1";

  src = builtins.path {
    path = ./.;
    name = "sllim-source";
    filter = gitFilter [''
      *.nix
    ''] ./.;
  };

  format = "pyproject";

  propagatedBuildInputs = with python3Packages; [
    aiohttp
    cbor2
  ];

  makeWrapperArgs = [
    # TODO: it would be nicer to have the Python module detect paths at build
    # time and save them, so we don't have to override PATH at run time.
    "--prefix" "PATH" ":" (lib.makeBinPath [ bcdb llvm ])
  ];

  postInstall = ''
    mv bin/* "$out"/bin/
    mv libexec "$out"
  '';
}
