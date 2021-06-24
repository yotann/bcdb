(import (fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/5de44c15758465f8ddf84d541ba300b48e56eda4.tar.gz";
  sha256 = "05darjv3zc5lfqx9ck7by6p90xgbgs1ni6193pw5zvi7xp2qlg4x";
}) {
  overlays = [
    (import ./bitcode.nix)
  ];
})
