{
  description = "The Bitcode Database";

  outputs = { self, nixpkgs }: let
    pkgs = nixpkgs.legacyPackages.x86_64-linux;
  in {

    packages.x86_64-linux = import ./.         { inherit pkgs; };
    devShell.x86_64-linux = import ./shell.nix { inherit pkgs; };

    defaultPackage.x86_64-linux = self.packages.x86_64-linux.bcdb;

    checks = self.packages;

  };
}
