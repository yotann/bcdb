(import (fetchTarball {
  url = "https://github.com/NixOS/nixpkgs/archive/5de44c15758465f8ddf84d541ba300b48e56eda4.tar.gz";
  sha256 = "05darjv3zc5lfqx9ck7by6p90xgbgs1ni6193pw5zvi7xp2qlg4x";
}) (let
  addCflags = flags: self: super: {
    stdenv = super.addAttrsToDerivation {
      NIX_CFLAGS_COMPILE = flags;
      hardeningDisable = ["fortify"];
    } super.stdenv;
    clangStdenv = super.addAttrsToDerivation {
      NIX_CFLAGS_COMPILE = "-O0";
      hardeningDisable = ["fortify"];
    } super.clangStdenv;
    libcxxStdenv = super.addAttrsToDerivation {
      NIX_CFLAGS_COMPILE = "-O0";
      hardeningDisable = ["fortify"];
    } super.libcxxStdenv;
  };
in {
  overlays = [
    (import ./bitcode.nix)
    (self: super: {
      pkgsO0 = self.extend (addCflags "-O0");
      pkgsO1 = self.extend (addCflags "-O1");
      pkgsO2 = self.extend (addCflags "-O2");
      pkgsO3 = self.extend (addCflags "-O3");
      pkgsOs = self.extend (addCflags "-Os");
      pkgsOz = self.extend (addCflags "-Oz");
    })
  ];
}))
