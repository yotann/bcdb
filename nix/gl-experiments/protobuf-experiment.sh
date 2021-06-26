#!/usr/bin/env bash
set -euo pipefail

total_stripped_size() {
  (find "$1/" -type f | while read -r f; do llvm-strip --strip-unneeded "$f" -o -; done) | wc -c
}

nix build --no-link -f . protobuf{1..13}.everything

mkdir -p results
echo num_versions,lto_size,gl_size | tee results/protobuf.csv
for i in {1..13}; do
  nix build -f . "protobuf${i}.everything" -o "result-protobuf${i}"
  LTO=$(total_stripped_size "result-protobuf$i/lto-packages")
  GUIDED=$(total_stripped_size "result-protobuf$i/gl-packages/interposable")
  echo "$i,$LTO,$GUIDED" | tee -a results/protobuf.csv
done
