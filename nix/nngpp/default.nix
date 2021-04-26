{ stdenv, fetchFromGitHub, cmake, nng }:

stdenv.mkDerivation {
  name = "nngpp";
  version = "1.3.0";
  src = fetchFromGitHub {
    owner = "cwzx";
    repo = "nngpp";
    rev = "nng-v1.3.0";
    sha256 = "1g3hl3islz6vsnqjxi1k00f8ggyc6s50l87r1mszf3idan4sp2wh";
  };
  nativeBuildInputs = [ cmake ];
  buildInputs = [ nng ];
  enableParallelBuilding = true;
}
