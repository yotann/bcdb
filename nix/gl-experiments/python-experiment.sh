#!/usr/bin/env bash
set -euo pipefail

nix-build -A python.everything -o result-python
rm -rf results/python*
./python-measure.py -o results/python result-python/lto result-python/gl/*
./python-analyse.py results/python/*/* > results/python.csv
./python-plot.py results/python.csv | tee results/python.txt
