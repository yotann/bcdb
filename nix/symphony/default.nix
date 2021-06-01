{ stdenv, lib, fetchFromGitHub, pkg-config, cgl, clp, coinutils, osi }:

stdenv.mkDerivation rec {
  pname = "symphony";
  version = "5.6.17";

  src = fetchFromGitHub {
    owner = "coin-or";
    repo = "SYMPHONY";
    rev = "release/${version}";
    sha256 = "1fq85fk56frixigmy2n5ziccksq552zpzym2zgc86sza0kmbg7m6";
  };

  nativeBuildInputs = [ pkg-config ];
  propagatedBuildInputs = [ cgl clp coinutils osi ];

  doCheck = true;
  enableParallelBuilding = true;

  meta = with lib; {
    homepage = "https://github.com/coin-or/SYMPHONY";
    description = "Generic MILP solver, callable library, and extensible framework";
    license = licenses.epl10;
  };
}
