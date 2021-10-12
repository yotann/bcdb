(import (fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/248936ea5700b25cfa9b7eaf8abe13a11fe15617.tar.gz";
  sha256 = "1hsmyzd0194l279pm8ahz2lp0lfmaza05j7cjmlz7ryji15zvcyx";
}) (let
in {
  overlays = [
    (import ./bitcode.nix)
    (import ./Oflags.nix)
  ];
}))
