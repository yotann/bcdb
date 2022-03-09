#!/bin/sh
set -eu
# shellcheck disable=SC2046
nix-build --no-out-link -A llvm10-assert.all -A llvm11-assert.all -A llvm12-assert.all -A llvm13-assert.all | cachix push bcdb
