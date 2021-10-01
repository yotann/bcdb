(import (fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/b5182c214fac1e6db9f28ed8a7cfc2d0c255c763.tar.gz";
  sha256 = "1q5pphh16lq7pch0ihad1mr6fll0gf6d1rv9z1wdmlzqlgyq50ix";
}) (let
in {
  overlays = [
    (import ./bitcode.nix)
    (import ./Oflags.nix)
  ];
}))
