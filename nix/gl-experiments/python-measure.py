#! /usr/bin/env nix-shell
#! nix-shell -i python3 -p linuxPackages.perf "python3.withPackages (ps: [ ps.pyperf ])"

import argparse
import os
import os.path

import util

parser = argparse.ArgumentParser(description="Run Python benchmarks.")
parser.add_argument("-o", dest="output_dir", required=True)
parser.add_argument("python_path", nargs="+")
parser.add_argument("--enable-perf", action="store_true")

args = parser.parse_args()

benchmarks = [
    b.split() for b in open("python-benchmarks.txt").read().strip().split("\n")
]
benchmarks = {b[0]: (b[1], b[2:]) for b in benchmarks}
# name: (filename, extra_args)


def generate_args_env(exe_path, benchmark, out, affinity):
    exe_path = os.path.abspath(exe_path)
    env = {
        "PATH": f'{exe_path}/bin:{os.environ["PATH"]}',
        "PYTHONHOME": exe_path,
        "PYTHONPATH": f"{exe_path}/lib/python3.7/site-packages",
    }
    bm, extra_args = benchmarks[benchmark]
    args = [
        f"{exe_path}/bin/python",
        f"../../third_party/pyperformance/pyperformance/benchmarks/{bm}",
        "--no-locale",
        "--affinity",
        affinity,
    ]
    args += [
        "--inherit-environ",
        "PYTHONPATH,PYTHONHOME,LD_DEBUG,LD_DEBUG_OUTPUT,LD_BIND_NOW",
    ]
    args += ["--append", f"{out}/pyperf.json"]
    args += ["--rigorous"]
    args += extra_args
    return (args, env)


util.run_benchmarks(
    generate_args_env,
    args.python_path,
    benchmarks.keys(),
    args.output_dir,
    enable_perf=args.enable_perf,
)
