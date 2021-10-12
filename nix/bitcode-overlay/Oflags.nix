# Overlay that provides pkgsO0, pkgsO1, etc.

self: super: let
  original = self;

  # The correct way to do this is to use extendMkDerivationArgs from
  # nixpkgs/pkgs/stdenv/adapters.nix. Unfortunately, we can't access the
  # variables we need to use that from outside of Nixpkgs.
  addCflagsStdenv = flags: stdenv: stdenv // {
    mkDerivation = args: stdenv.mkDerivation (args // {
      NIX_CFLAGS_COMPILE = toString (args.NIX_CFLAGS_COMPILE or "") + " " + flags;
      hardeningDisable = (args.hardeningDisable or []) ++ ["format" "fortify"];
    });
  };

  addCflags = flags: self: super: {
    stdenv = addCflagsStdenv flags original.stdenv;
    clangStdenv = addCflagsStdenv flags original.clangStdenv;
    libcxxStdenv = addCflagsStdenv flags original.libcxxStdenv;

    # glibc requires optimizations
    # http://devpit.org/wiki/Gnu_Toolchain/Compatibility_Matrix#endnote_ODonell_and_Drepper_on_Inline
    inherit (original) glibc;

    # Requires optimizations to delete code that refers to frenchlib...
    # or we can just enable frenchlib.
    libuninameslist = super.libuninameslist.overrideAttrs (o: {
      configureFlags = (o.configureFlags or []) ++ ["--enable-frenchlib"];
    });
  };
in {
  pkgsO0 = self.extend (addCflags "-O0");
  pkgsO1 = self.extend (addCflags "-O1");
  pkgsO2 = self.extend (addCflags "-O2");
  pkgsO3 = self.extend (addCflags "-O3");
  pkgsOs = self.extend (addCflags "-Os");
  pkgsOz = self.extend (addCflags "-Oz");
}
