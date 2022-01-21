import argparse
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description="Optimize LLVM bitcode for size.")
parser.add_argument(
    "input", type=str, nargs="?", default="-", help="input path (stdin by default)"
)
parser.add_argument(
    "-o", "--output", type=str, default="-", help="output path (stdout by default)"
)
parser.add_argument(
    "-f", "--force", action="store_true", help="force binary output to TTY"
)


def optimize(data):
    import os
    from subprocess import check_call, check_output

    def which(cmd):
        import shutil

        path = shutil.which(cmd)
        if path is None:
            raise FileNotFoundError(f"program '{cmd}' not found in PATH")
        return Path(path).resolve()

    memodb_path = Path("/tmp/sllim.db")
    os.environ["MEMODB_STORE"] = f"sqlite:{memodb_path}"
    if not memodb_path.exists():
        try:
            check_call(("memodb", "init"))
        except Exception:
            # FIXME: ignoring errors because there's a race condition.
            pass

    level = int(os.environ.get("SLLIM_LEVEL", "3"))

    opt = [
        which("opt"),
        "-load-pass-plugin",
        which("bcdb").parent.parent / "lib" / "BCDBOutliningPlugin.so",
    ]

    if level >= 2:
        data = check_output(
            (
                *opt,
                "--passes",
                "remove-function-attr<optnone>,remove-function-attr<noinline>,add-function-attr<minsize>,add-function-attr<optsize>",
            ),
            input=data,
        )
    data = check_output((*opt, "-Oz"), input=data)

    if level >= 3:
        import hashlib

        head = hashlib.blake2b(data).hexdigest()
        check_output(("bcdb", "add", "--name", head, "-"), input=data)
        cid = (
            check_output(
                (
                    "smout",
                    "optimize",
                    "--compile-all-callers",
                    "-j=all",
                    "--max-nodes=200",
                    "--name",
                    head,
                )
            )
            .decode("ascii")
            .strip()
        )
        data = check_output(("bcdb", "get", cid))
        data = check_output(
            (*opt, "--passes", "function(simplifycfg),function-attrs"), input=data
        )

    llc_args = check_output(("bc-imitate", "llc-args", "-"), input=data).decode("ascii")
    llc_args = [arg for arg in llc_args.split("\n") if arg]
    llc_args.append("-O=2")
    llc_args.append("-filetype=obj")

    if level >= 1:
        llc_args.append("--cost-kind=code-size")
        # Note that many targets enable the machine outliner by default, so
        # this option might not matter.
        llc_args.append("--enable-machine-outliner=always")
        # 5 rounds total, same as Chabbi paper.
        llc_args.append("--machine-outliner-reruns=4")

    data = check_output(
        (
            "llc",
            *llc_args,
        ),
        input=data,
    )

    return data


def main():
    args = parser.parse_args()
    if args.input == "-":
        if sys.stdin.buffer.isatty() and not args.force:
            print("You must provide a bitcode module.", file=sys.stderr)
            sys.exit(1)
        data = sys.stdin.buffer.read()
    else:
        data = Path(args.input).read_bytes()
    data = optimize(data)
    if args.output == "-":
        if sys.stdout.buffer.isatty() and not args.force:
            print(
                "Refusing to write binary data to stdout. You can force output with the '-f' option.",
                file=sys.stderr,
            )
            sys.exit(1)
        sys.stdout.buffer.write(data)
    else:
        Path(args.output).write_bytes(data)
