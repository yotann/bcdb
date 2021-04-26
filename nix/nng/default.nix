{ stdenv, fetchFromGitHub, cmake }:

stdenv.mkDerivation {
  name = "nng";
  version = "1.4.0";
  src = fetchFromGitHub {
    owner = "nanomsg";
    repo = "nng";
    rev = "v1.4.0";
    sha256 = "0l26yz4ikwjb9kfyh7ka4xj2klxp5cfl6ps9b1xbg1xzsjlvm1f8";
  };
  nativeBuildInputs = [ cmake ];
  doCheck = false; # requires network access
  enableParallelBuilding = true;
}
