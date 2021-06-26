#!/usr/bin/env bash
set -euo pipefail

total_stripped_size() {
  (find "$1/" -type f | while read -r f; do llvm-strip --strip-unneeded "$f" -o -; done) | wc -c
}

nix build --no-link -f . boost{1..14}.everything

mkdir -p results
echo num_versions,lto_size,gl_size | tee results/boost.csv
for i in {1..11}; do
  nix build -f . "boost${i}.everything" -o "result-boost${i}"
  LTO=$(total_stripped_size "result-boost$i/lto-packages")
  GUIDED=$(total_stripped_size "result-boost$i/gl-packages/interposable")
  echo "$i,$LTO,$GUIDED" | tee -a results/boost.csv
done
