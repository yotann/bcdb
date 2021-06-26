# Guided Linking Experiments

This directory contains scripts that run the experiments in our [Guided Linking
paper](../../docs/guided-linking) using Nix. You can also modify the files
(especially `experiments.nix`) to set up your own experiments.

## Structure of the experiments

If you run `python-experiment.sh`, this is how all the parts interact:

- `nix-build -A python.everything` is run to build Python with and without
  guided linking. It evaluates `default.nix`.
  - `experiments.nix` provides the Python-specific configuration for guided
    linking, such as the list of packages to build.
  - `../bitcode-overlay` provides a version of the Nixpkgs package collection
    that is automatically built with embedded bitcode.
    - It uses `../bitcode-cc-wrapper`, a wrapper for Clang that automatically
      adds the `-fembed-bitcode` option.
  - The `bc-imitate`, `memodb init`, and `bcdb add` commands are used to fill a
    BCDB instance with bitcode extracted from the packages.
  - The `bcdb gl` command is used to actually perform Guided Linking.
  - Clang is used to optimize, compile, and link the merged library and other
    code.
- `python-measure.py` benchmarks the different versions of Python and produces
  lots of performance data.
  - `util.py` distributes the benchmarks across different CPU cores and manages
    some other things.
- `python-analyse.py` summarizes the performance data into a big CSV file.
- `python-plot.py` summarizes the data further into a few numbers and produces
  a plot PDF.

## System requirements

To reproduce all the existing experiments here, the minimum requirements are an
x86-64 CPU core, 8-12GB RAM, and 30-50GB hard disk space. But with only one
core, reproducing the results could take more than a week. We recommend using
as many cores as possible, as long as there is at least 8GB RAM per core.

You can cut down on compilation time by using our [Cachix](https://cachix.org)
cache, which includes prebuilt versions of bitcode packages. Simply install
Cachix and run `cachix use bcdb`.

## Evaluating Boost and Protobuf

Simply run `./boost-experiment.sh` or `./protobuf-experiment.sh`. The results
will be placed in `results/boost.csv` or `results/protobuf.csv`, measuring the
total stripped ELF file size with and without guided linking.

## Evaluating Python

Run `./python-experiment.sh`. The most detailed results will go in
`results/python.csv`; a summary will be put in `results/python.txt` and a plot
in `results/python-speedup.pdf`.

## Evaluating Clang

TODO: finish setting this up.
