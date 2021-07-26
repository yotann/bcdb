#!/usr/bin/env python3
import getopt
import re
import subprocess
import sys

try:
    from shlex import quote
except ImportError:
    _find_unsafe = re.compile(r"[^\w@%+=:,./-]").search

    def quote(s):
        if s and _find_unsafe(s) is None:
            return s
        return "'" + s.replace("'", "'\\''") + "'"


def trace_args(args):
    print(" ".join(quote(a) for a in args))


opts, args = getopt.gnu_getopt(
    sys.argv[1:],
    "",
    ["allow-nonzero-return", "no-run"],
)
filename = None
lli_pre_args = []
lli_post_args = []
for arg in args:
    if filename is None and not arg.startswith("-"):
        filename = arg
    elif filename is None:
        lli_pre_args.append(arg)
    else:
        lli_post_args.append(arg)

allow_nonzero_return = False
enable_lli = True
for opt, value in opts:
    if opt == "--no-run":
        enable_lli = False
    elif opt == "--allow-nonzero-return":
        allow_nonzero_return = True

if enable_lli:
    # Run original code to determine expected output.
    args = ["lli"] + lli_pre_args + ["-"] + lli_post_args
    trace_args(args)
    p = subprocess.run(args, input=open(filename, "rb").read(), capture_output=True)
    if not allow_nonzero_return:
        p.check_returncode()
    expected_returncode = p.returncode
    expected_stdout = p.stdout
    expected_stderr = p.stderr


class Candidate:
    def __init__(self, function, nodes):
        self.function = function.decode("ascii")
        self.nodes = nodes.decode("ascii").replace(" ", "")
        self.node_set = set()
        for span in nodes.split(b","):
            if b"-" in span:
                first, last = span.split(b"-")
                self.node_set |= set(range(int(first), int(last) + 1))
            else:
                self.node_set.add(int(span))

    def __str__(self):
        return f"{self.function}:{self.nodes}"

    def overlaps(self, other):
        return self.function == other.function and not self.node_set.isdisjoint(
            other.node_set
        )


# List all candidates.
args = [
    "opt",
    "--load=BCDBOutliningPlugin.so",
    "--outlining-candidates",
    "--outline-unprofitable",
    "--analyze",
    filename,
]
trace_args(args)
output = subprocess.check_output(args)
cur_function = None
candidates = []
for function, nodes in re.findall(
    br"'Outlining Candidates.*' for function '(.*)':$|^candidate: \[([^]]*)\]",
    output,
    re.M,
):
    if function:
        cur_function = function
    if nodes:
        candidates.append(Candidate(cur_function, nodes))

candidates.sort(key=lambda candidate: (candidate.function, candidate.nodes))

# Test outlining with all candidates. Outline multiple candidates at once when
# possible.
while candidates:
    chosen = [candidates.pop()]
    for i in range(len(candidates) - 1, -1, -1):
        if not any(x.overlaps(candidates[i]) for x in chosen):
            chosen.append(candidates.pop(i))

    args = ["opt", "--load=BCDBOutliningPlugin.so", "--outlining-extractor", filename]
    for x in chosen:
        args.append(f"--outline-only={x}")
    trace_args(args)
    bitcode = subprocess.check_output(args)

    if enable_lli:
        args = ["lli"] + lli_pre_args + ["-"] + lli_post_args
        trace_args(args)
        p = subprocess.run(args, input=bitcode, capture_output=True)
        if p.returncode != expected_returncode:
            print("Return code mismatch!")
            sys.exit(1)
        if p.stdout != expected_stdout:
            print("stdout mismatch!")
            sys.exit(1)
        if p.stderr != expected_stderr:
            print("stdout mismatch!")
            sys.exit(1)
