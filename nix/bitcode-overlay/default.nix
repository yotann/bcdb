(import (fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/40affc01a03985dacfa3c67b77b49eb7191058c3.tar.gz";
  sha256 = "1mvdw2908knh4knfyx7av9n2fga725066z5ds2sic1vg25ijd981";
}) {
  overlays = [
    (import ./bitcode.nix)
  ];
})
