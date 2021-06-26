#! /usr/bin/env nix-shell
#! nix-shell -i python3 -p linuxPackages.perf "python3.withPackages (ps: [ ps.pyperf ])"

import argparse
import functools
import shlex
import subprocess

import util

parser = argparse.ArgumentParser(description="Run Clang benchmarks.")
parser.add_argument("-o", dest="output_dir", required=True)
parser.add_argument("clang_path", nargs="+")
parser.add_argument("--enable-perf", action="store_true")

args = parser.parse_args()


@functools.lru_cache(None)
def get_clang_cmdline(exe_path):
    p = subprocess.run(
        (
            "nix",
            "eval",
            "--raw",
            "-f",
            "clang-cmdline.nix",
            "--argstr",
            "exepath",
            exe_path,
            "args",
        ),
        capture_output=True,
        text=True,
    )
    p.check_returncode()
    cmdline = p.stdout
    return shlex.split(p.stdout)


def generate_args_env(exe_path, benchmark, out, affinity):
    args = [
        f"{exe_path}/bin/clang",
        "-fPIC",
        "-shared",
        "-O3",
        "../../third_party/sqlite/sqlite-amalgamation-3320000.c",
        "-ldl",
        "-lpthread",
        "-o",
        "/dev/null",
    ]
    args.extend(get_clang_cmdline(exe_path))
    args = [
        "pyperf",
        "command",
        "-p20",
        "-n1",
        "-w1",
        "--no-locale",
        "--affinity",
        affinity,
        "--inherit-environ",
        "LD_DEBUG,LD_DEBUG_OUTPUT,LD_BIND_NOW,PYTHONPATH",
        "--append",
        f"{out}/pyperf.json",
    ] + args
    return (args, {})


util.run_benchmarks(
    generate_args_env,
    args.clang_path,
    ["sqlite"],
    args.output_dir,
    enable_perf=args.enable_perf,
)
