#!/usr/bin/env bash
set -euo pipefail

total_stripped_size() {
  (find "$1/" -type f | while read -r f; do llvm-strip --strip-unneeded "$f" -o -; done) | wc -c
}

nix-build -A clang.everything -o result-clang
rm -rf results/clang
./clang-measure.py -o results/clang result-clang/lto result-clang/gl/*
# FIXME: analyse
