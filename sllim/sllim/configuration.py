import os

from .memodb import *
from .passes import *
from .runner import *


class Config:
    def __init__(self):
        self.level = int(os.environ.get("SLLIM_LEVEL", "5"))
        self.passes = []
        self.add_default_passes()

    def add_default_passes(self):
        if self.level >= 2:
            self.passes.append(ForceMinSizePass(self))
        if self.level >= 1:
            self.passes.append(StandardPass(self))
        if self.level >= 7:
            self.passes.append(SmoutPass(self))
        self.passes.append(CompilePass(self))

    async def optimize(self, store, data):
        runner = Runner(store)
        link = await runner.add_module(data)
        for pass_ in self.passes:
            # TODO: try every possible combination of params.
            params = {key: value[-1] for key, value in pass_.params.items()}
            params_link = Link(store, node=params)
            call = Call(
                f"sllim.{type(pass_).__name__}",
                (await params_link.as_cid(), await link.as_cid()),
            )
            if result := await store.get_optional(call):
                link = result
            else:
                link = await pass_.process(runner, params, link)
                await store.set(call, cid=await link.as_cid())
        return await link.as_node()
