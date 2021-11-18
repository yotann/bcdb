#! /usr/bin/env nix-shell
#! nix-shell -i python -p "python3.withPackages (ps: [ps.aiohttp ps.cbor2 ps.hydra ps.joblib])"

import asyncio
import os

from memodb import *


async def analyze(name):
    async with Store(os.getenv("MEMODB_STORE")) as store:
        experiments = await store.get(name)

        for k, v in experiments.items():
            v = await v.as_node()
            v = await v["vars"]["compiled_size"].as_node()
            size = int(v.split(b"\n")[1].lstrip().split()[0])
            print(size, k)


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2 or not sys.argv[1].startswith("/cid/"):
        print(f"Usage: {sys.argv[0]} /cid/...")
        print("Use the 'All Experiments Result' from runner.log")
        sys.exit(1)
    name = Name.parse_url(sys.argv[1])
    asyncio.run(analyze(name))
