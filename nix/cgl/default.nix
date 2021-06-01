{ stdenv, lib, fetchFromGitHub, pkg-config, clp, coinutils, osi }:

stdenv.mkDerivation rec {
  pname = "cgl";
  version = "0.60.3";

  src = fetchFromGitHub {
    owner = "coin-or";
    repo = "Cgl";
    rev = "releases/${version}";
    sha256 = "0036230i3rrr4vlhnsgsnvzp70hni2w8zk67xxrv06dp62ah03s7";
  };

  nativeBuildInputs = [ pkg-config ];
  propagatedBuildInputs = [ clp coinutils osi ];

  doCheck = true;
  enableParallelBuilding = true;

  meta = with lib; {
    homepage = "https://github.com/coin-or/Cgl";
    description = "COIN-OR Cut Generation Library";
    license = licenses.epl10;
  };
}
