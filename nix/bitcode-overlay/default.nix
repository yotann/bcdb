(import (fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/5de44c15758465f8ddf84d541ba300b48e56eda4.tar.gz";
  sha256 = "0nbhqlgfag1l0bmg141nsbw9jh124rxmaim4qzay8c20b2rlrcwn";
}) {
  overlays = [
    (import ./bitcode.nix)
  ];
})
