{ pkgs ? import nix/import-flake-lock.nix, ...
}:

pkgs.mkShell {
  inputsFrom = [ (import ./. { inherit pkgs; }).bcdb-debug ];
}
