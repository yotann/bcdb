#!/bin/sh
set -eu
# shellcheck disable=SC2046
nix-build --no-out-link -A llvm11-assert.all -A llvm12-assert.all -A llvm13-assert.all -A llvm14-assert.all | cachix push bcdb
