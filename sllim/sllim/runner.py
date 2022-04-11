import asyncio
from pathlib import Path

from .memodb import *


def which(cmd):
    import shutil

    path = shutil.which(cmd)
    if path is None:
        raise FileNotFoundError(f"program '{cmd}' not found in PATH")
    return Path(path).resolve()


class Runner:
    def __init__(self, store):
        self.store = store
        self.bcdb_path = which("bcdb")
        self.opt_path = [
            which("opt"),
            "--cost-kind=code-size",
            "-load-pass-plugin",
            which("bcdb").parent.parent / "lib" / "BCDBOutliningPlugin.so",
        ]

    async def check_output(self, program, *args, input=b""):
        from asyncio.subprocess import PIPE
        from subprocess import CalledProcessError

        proc = await asyncio.create_subprocess_exec(
            program, *args, stdin=PIPE, stdout=PIPE, stderr=PIPE
        )
        stdout, stderr = await proc.communicate(input)
        if proc.returncode != 0:
            print(stderr)
            raise CalledProcessError(proc.returncode, program, stdout, stderr)
        return stdout

    async def add_module(self, data):
        result = await self.check_output(
            self.bcdb_path, "add", "--no-head", "-", input=data
        )
        cid = Name.parse_url(result.strip().decode("ascii"))
        return Link(self.store, cid=cid)

    async def get_module(self, link):
        result = await self.check_output(self.bcdb_path, "get", "--", str(link))
        return result

    async def cmd(self, *args, input, input_fmt, output_fmt):
        if input_fmt == "bc":
            input = await self.get_module(input)
        else:
            assert False, input_fmt
        output = await self.check_output(*args, input=input)
        if output_fmt in ("bin", "o"):
            pass
        elif output_fmt == "txt":
            output = output.decode("utf-8")
        elif output_fmt == "bc":
            return await self.add_module(output)
        else:
            assert False, output_fmt
        return Link(self.store, node=output)

    async def opt(self, link, *args):
        return await self.cmd(
            *self.opt_path, *args, input=link, input_fmt="bc", output_fmt="bc"
        )
