# Overlay that provides pkgsO0, pkgsO1, etc.

self: super: let
  original = self;

  addCflags = flags: self: super: {
    stdenv = super.addAttrsToDerivation {
      NIX_CFLAGS_COMPILE = flags;
      hardeningDisable = ["format" "fortify"];
    } original.stdenv;
    clangStdenv = super.addAttrsToDerivation {
      NIX_CFLAGS_COMPILE = "-O0";
      hardeningDisable = ["format" "fortify"];
    } original.clangStdenv;
    libcxxStdenv = super.addAttrsToDerivation {
      NIX_CFLAGS_COMPILE = "-O0";
      hardeningDisable = ["format" "fortify"];
    } original.libcxxStdenv;

    # glibc requires optimizations
    # http://devpit.org/wiki/Gnu_Toolchain/Compatibility_Matrix#endnote_ODonell_and_Drepper_on_Inline
    inherit (original) glibc;
  };
in {
  pkgsO0 = self.extend (addCflags "-O0");
  pkgsO1 = self.extend (addCflags "-O1");
  pkgsO2 = self.extend (addCflags "-O2");
  pkgsO3 = self.extend (addCflags "-O3");
  pkgsOs = self.extend (addCflags "-Os");
  pkgsOz = self.extend (addCflags "-Oz");
}
