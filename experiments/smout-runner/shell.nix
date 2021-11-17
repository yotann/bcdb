{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  nativeBuildInputs = [
    (pkgs.python3.withPackages (ps: [
      ps.aiohttp
      ps.cbor2
      ps.hydra
      ps.joblib
    ]))
  ];
}
