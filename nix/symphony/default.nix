{ stdenv, lib, fetchFromGitHub, pkg-config, cgl, clp, coinutils, osi, llvmPackages ? null }:

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

  buildInputs = lib.optionals stdenv.cc.isClang [
    # TODO: This may mismatch the LLVM version in the stdenv, see nixpkgs#79818.
    llvmPackages.openmp
  ];

  configureFlags = [
    "--enable-openmp"
  ];

  doCheck = true;
  enableParallelBuilding = true;

  meta = with lib; {
    homepage = "https://github.com/coin-or/SYMPHONY";
    description = "Generic MILP solver, callable library, and extensible framework";
    license = licenses.epl10;
  };
}
