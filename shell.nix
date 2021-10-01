args @ { pkgs ? import nix/import-flake-lock.nix, ...
}:

pkgs.mkShell {
  inputsFrom = [ (import ./. args).bcdb-debug ];
}
