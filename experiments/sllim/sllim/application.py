import argparse
import asyncio
from asyncio.subprocess import PIPE
import fcntl
from pathlib import Path
import os
import sys

from . import configuration
from . import memodb

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


async def main_with_server(data):
    async with memodb.Store(os.environ["MEMODB_STORE"]) as store:
        config = configuration.Config()
        data = await config.optimize(store, data)
    return data


async def async_main(data):
    cache_home = None
    if "XDG_CACHE_HOME" in os.environ:
        cache_home = Path(os.environ["XDG_CACHE_HOME"])
        if not cache_home.is_absolute():
            cache_home = None
    if cache_home is None:
        cache_home = Path.home() / ".cache"
    cache_home /= "sllim"
    cache_home.mkdir(parents=True, exist_ok=True)

    with (cache_home / "memodb-server.lock").open("w") as lock_fp:
        # FIXME: Avoid using a TCP port. This is fine in Docker (which uses a
        # firewall by default), but it's a security problem otherwise.
        #
        # TODO: Allow multiple instances of sllim to do processing at once. We
        # would need some way to start memodb-server with the first sllim
        # command and wait to kill it until the last sllim command ends.
        fcntl.flock(lock_fp, fcntl.LOCK_EX)
        memodb_path = cache_home / "memodb.rocksdb"
        memodb_store = f"rocksdb:{memodb_path}"
        memodb_http = "http://127.0.0.1:17633"
        if not memodb_path.exists():
            import subprocess

            subprocess.check_call(("memodb", "init", "--store", memodb_store))
        server_process = None
        worker_process = None
        try:
            server_process = await asyncio.create_subprocess_exec(
                "memodb-server",
                "--store",
                memodb_store,
                memodb_http,
                stderr=PIPE,
            )
            # Wait for the server to actually start (it can take a long time if
            # RocksDB needs to replay database logs).
            await server_process.stderr.readline()

            os.environ["MEMODB_STORE"] = memodb_http
            worker_process = await asyncio.create_subprocess_exec("smout", "worker")
            return await main_with_server(data)
        finally:
            if worker_process:
                worker_process.terminate()
                await worker_process.wait()
            if server_process:
                server_process.terminate()


def main():
    args = parser.parse_args()
    if args.input == "-":
        if sys.stdin.buffer.isatty() and not args.force:
            print("You must provide a bitcode module.", file=sys.stderr)
            sys.exit(1)
        data = sys.stdin.buffer.read()
    else:
        data = Path(args.input).read_bytes()

    data = asyncio.run(async_main(data))

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
