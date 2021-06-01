{ stdenv, lib, fetchFromGitHub }:

stdenv.mkDerivation rec {
  pname = "coinutils";
  version = "2.11.4";

  src = fetchFromGitHub {
    owner = "coin-or";
    repo = "CoinUtils";
    rev = "releases/${version}";
    sha256 = "11z9lq6mbqk7y0p3jmz5qaal98h7bm4qzp1v3n8gc7rzr3xjdlqk";
  };

  doCheck = true;
  enableParallelBuilding = true;

  meta = with lib; {
    homepage = "https://www.github.com/coin-or/CoinUtils";
    description = "Collection of utilities used by other COIN-OR projects";
    license = licenses.epl10;
  };
}
