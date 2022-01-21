#! /usr/bin/env nix-shell
#! nix-shell -i python -p "python3.withPackages (ps: [ps.aiohttp ps.cbor2 ps.hydra ps.joblib])"

import asyncio
from dataclasses import dataclass
import logging
from pathlib import Path
from typing import Any

import hydra
from hydra.core.config_store import ConfigStore
from omegaconf import OmegaConf

from memodb import *


log = logging.getLogger(__name__)


def which(cmd):
    import shutil

    path = shutil.which(cmd)
    if path is None:
        raise FileNotFoundError(f"program '{cmd}' not found in PATH")
    return Path(path).resolve()


@dataclass
class Var:
    name: str


@dataclass
class CallConfig:
    func: str
    args: tuple[Any]


@dataclass
class RunnerConfig:
    calls: dict[str, CallConfig]
    files: dict[str, str]
    params: dict[str, Any]


class SmoutRunner:
    def __init__(self):
        import os

        self.bcdb = which("bcdb")
        self.llc = which("llc")
        self.llvm_size = which("llvm-size")
        self.llvm_objdump = which("llvm-objdump")
        self.bcdb_path = self.bcdb.parent.parent
        self.opt = [
            which("opt"),
            "-load-pass-plugin",
            self.bcdb_path / "lib" / "BCDBOutliningPlugin.so",
        ]

        if os.getenv("MEMODB_STORE") is None:
            raise ValueError(f"you must set MEMODB_STORE=")
        self.store_uri = os.getenv("MEMODB_STORE")

    async def resolve_link(self, vars, node):
        if isinstance(node, Var):
            return await vars[node.name]
        return Link(self.store, node=node)

    async def file_task(self, path: Path) -> Link:
        data = path.read_bytes()
        return Link(self.store, node=data)

    async def check_output(self, program, *args, input=b""):
        from asyncio.subprocess import PIPE
        from subprocess import CalledProcessError

        proc = await asyncio.create_subprocess_exec(
            program, *args, stdin=PIPE, stdout=PIPE, stderr=PIPE
        )
        stdout, stderr = await proc.communicate(input)
        if proc.returncode != 0:
            log.error(f"Stderr: {stderr}")
            raise CalledProcessError(proc.returncode, program, stdout, stderr)
        return stdout

    async def call_uncached(self, call: Call, args: list[Link]) -> Link:
        if call.func == "bcdb.split":
            assert len(args) == 1
            result = await self.check_output(
                self.bcdb, "add", "--no-head", "-", input=await args[0].as_node()
            )
            cid = Name.parse_url(result.strip().decode("ascii"))
            return Link(self.store, cid=cid)
        elif call.func == "cmd.opt":
            assert len(args) == 2
            bc = await self.check_output(self.bcdb, "get", str(await args[1].as_cid()))
            bc = await self.check_output(
                *self.opt, *(await args[0].as_node()), input=bc
            )
            result = await self.check_output(
                self.bcdb, "add", "--no-head", "-", input=bc
            )
            cid = Name.parse_url(result.strip().decode("ascii"))
            return Link(self.store, cid=cid)
        elif call.func == "cmd.llc":
            assert len(args) == 2
            bc = await self.check_output(self.bcdb, "get", str(await args[1].as_cid()))
            result = await self.check_output(
                self.llc, *(await args[0].as_node()), input=bc
            )
            return Link(self.store, node=result)
        elif call.func == "cmd.llvm-size":
            assert len(args) == 2
            result = await self.check_output(
                self.llvm_size,
                *(await args[0].as_node()),
                input=await args[1].as_node(),
            )
            return Link(self.store, node=result)
        elif call.func == "cmd.llvm-objdump":
            assert len(args) == 2
            result = await self.check_output(
                self.llvm_objdump,
                *(await args[0].as_node()),
                input=await args[1].as_node(),
            )
            return Link(self.store, node=result)
        else:
            return await self.store.evaluate(call)

    async def call_task(self, vars, cfg: CallConfig) -> Link:
        args = await asyncio.gather(*(self.resolve_link(vars, arg) for arg in cfg.args))
        if cfg.func == "id":
            assert len(args) == 1
            return args[0]
        call = Call(cfg.func, tuple([await arg.as_cid() for arg in args]))
        log.info(f"Calling {call}")
        cached = await self.store.get_optional(call)
        if cached is not None:
            assert isinstance(cached, Link)
            return cached
        result = await self.call_uncached(call, args)
        result_cid = await result.as_cid()
        log.info(f"Called {call}: result {result_cid}")
        await self.store.set(call, link=result)
        return result

    async def run(self, cfg):
        vars = {}
        for name, path in cfg.files.items():
            vars[name] = asyncio.create_task(self.file_task(Path(path)))
        for name, call_cfg in cfg.calls.items():
            vars[name] = asyncio.create_task(self.call_task(vars, call_cfg))
        await asyncio.wait(vars.values())

        result = {"config": OmegaConf.to_yaml(cfg), "vars": {}}
        for name, task in vars.items():
            result["vars"][name] = await task
        result_cid = await self.store.add(result)
        log.info(f"Experiment Result: {result_cid}")
        return result_cid

    async def run_multi(self, cfgs):
        async with Store(self.store_uri) as store:
            self.store = store
            tasks = {x: asyncio.create_task(self.run(cfg)) for x, cfg in cfgs.items()}
            await asyncio.wait(tasks.values())
            result = {x: Link(self.store, cid=await task) for x, task in tasks.items()}
            result_cid = await self.store.add(result)
            log.info(f"All Experiments Result: {result_cid}")


OmegaConf.register_new_resolver("var", Var)
cs = ConfigStore.instance()
cs.store(name="runner_schema", node=RunnerConfig)


if __name__ == "__main__":
    import sys

    log_fmt = logging.Formatter("[%(asctime)s][%(name)s][%(levelname)s] - %(message)s")
    log_root = logging.getLogger()
    log_root.setLevel(logging.INFO)
    for log_handler in (logging.FileHandler("runner.log"), logging.StreamHandler()):
        log_handler.setFormatter(log_fmt)
        log_root.addHandler(log_handler)

    if len(sys.argv) != 2 or not sys.argv[1].startswith("+experiment="):
        print(f"Usage: {sys.argv[0]} +experiment=smout,smout_mo,...")
        print("See experiment/ directory for possible experiments.")
        sys.exit(1)
    log.warning("Make sure you're using the latest version of smout and smout-runner!")
    experiments = sys.argv[1][12:].split(",")

    # Not using hydra.main() because it doesn't support async.
    hydra.initialize(config_path="config")
    cfgs = {
        x: hydra.compose(config_name="config", overrides=[f"+experiment={x}"])
        for x in experiments
    }

    runner = SmoutRunner()
    asyncio.run(runner.run_multi(cfgs))
