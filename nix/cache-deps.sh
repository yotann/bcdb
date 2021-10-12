#!/bin/sh
set -eu
# shellcheck disable=SC2046
nix-build --no-out-link -A llvm10-assert -A llvm11-assert -A llvm12-assert -A llvm13-assert -A nng | cachix push bcdb
