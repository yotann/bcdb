import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

# This script reads a file containing shell commands and their expected output,
# runs the commands, and makes sure the output is correct. It can also
# initialize a MemoDB store and/or start a memodb-server instance.
#
# Example input file:
# $ echo hello
# hello
# $ cat <<EOF
# test
# EOF
# test
# $ not false
#
# Heredocs must use the notation `<<EOF` exactly (with no spaces). Backslashes
# at the end of a line are not supported. Otherwise, commands are passed to the
# shell as-is.
#
# Each command line must exit with status 0, or else the test will fail. The
# command's stdout and stderr will be combined and checked against the expected
# output. The sequence "..." can be used as a wildcard in the expected output;
# see below.
#
# Related projects:
# https://github.com/endocode/shelldoc
# https://specdown.io/
# https://zimbatm.github.io/mdsh/


def expected_output_to_re(expected):
    # The command's expected output must exactly match its actual output,
    # except that "..." is a wildcard. "..." on a line by itself matches any
    # number of lines of text, while "..." on a line with other text matches
    # any sequence of non-newline characters.

    def sub(m):
        if m.group(1):
            return "(^.*\\n)*"
        elif m.group(2):
            return ".*"
        elif m.group(3):
            return m.group(3)
        else:
            return "\\" + m.group(4)

    expected_re = re.sub(
        r"(^\.\.\.\n)|(\.\.\.)|([A-Za-z0-9]+)|(.)", sub, expected, flags=re.M | re.S
    )
    return re.compile(expected_re, re.M)


parser = argparse.ArgumentParser(description="Test shell commands.")
parser.add_argument(
    "input", metavar="FILE", type=Path, help="the file containing test commands"
)
parser.add_argument(
    "--tmpdir", "-t", type=Path, help="temporary directory (WILL BE REMOVED)"
)
parser.add_argument(
    "--with-store", help="initialize a MemoDB store and set MEMODB_STORE"
)
args = parser.parse_args()

if args.tmpdir:
    tmpdir = args.tmpdir
elif path := os.getenv("XDG_RUNTIME_DIR"):
    tmpdir = Path(path) / "shelltest"
else:
    tmpdir = Path("/tmp") / "shelltest"
if tmpdir.exists():
    shutil.rmtree(tmpdir)
tmpdir.mkdir(exist_ok=True)
print("Working directory:", tmpdir)
os.chdir(tmpdir)

stdin_file = tmpdir / "stdin"

server_process = None
if args.with_store is None:
    pass
elif args.with_store in "sqlite":
    memodb_path = tmpdir / "memodb.sqlite"
    if memodb_path.exists():
        memodb_path.unlink()
    os.environ["MEMODB_STORE"] = f"sqlite:{memodb_path}"
    subprocess.run(("memodb", "init"), check=True)
elif args.with_store in ("rocksdb", "client"):
    # Use RocksDB backend for the server (SQLite is too slow for the
    # ackermann/nqueens tests).
    memodb_path = tmpdir / "memodb.rocksdb"
    if memodb_path.exists():
        shutil.rmtree(memodb_path)
    os.environ["MEMODB_STORE"] = f"rocksdb:{memodb_path}"
    subprocess.run(("memodb", "init"), check=True)
else:
    print("Unsupported store type:", args.with_store)
    sys.exit(1)

if args.with_store == "client":
    socket_path = tmpdir / "memodb.socket"
    if socket_path.exists():
        socket_path.unlink()
    server_process = subprocess.Popen(("memodb-server", f"unix:{socket_path}"))
    os.environ["MEMODB_STORE"] = f"unix:{socket_path}"
    while not socket_path.exists():
        time.sleep(1e-2)

print("MEMODB_STORE:", os.environ.get("MEMODB_STORE"))

try:
    for cmd, expected in re.findall(
        r"^\$ (.*)\n((?:[^$].*\n)*)", args.input.read_text(), re.M
    ):
        stdin = ""
        if "<<EOF" in cmd:
            i = expected.index("\nEOF\n")
            stdin = expected[: i + 1]
            expected = expected[i + 5 :]
            cmd = cmd.replace("<<EOF", "<" + shlex.quote(str(stdin_file)))

        expected_re = expected_output_to_re(expected)

        stdin_file.write_text(stdin)
        print("$", cmd)
        p = subprocess.run(
            (cmd,),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            shell=True,
        )
        actual_output = p.stdout.decode("utf-8", errors="replace")
        print(actual_output, end="")
        if p.returncode:
            print(f"ERROR: command {cmd!r} failed (status {p.returncode})")
            sys.exit(1)
        if not expected_re.fullmatch(actual_output):
            print(f"output: {actual_output!r}")
            print(f"expected pattern: {expected_re.pattern!r}")
            print(f"ERROR: command {cmd!r} output doesn't match")
            sys.exit(1)
finally:
    if server_process is not None:
        server_process.terminate()
        server_process.wait()
