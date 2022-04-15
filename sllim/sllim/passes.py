import collections

from .memodb import *


class Pass:
    def __init__(self, config):
        self.params = collections.defaultdict(lambda: [])
        self.add_default_params(config)

    def add_default_params(self, config):
        pass

    def add_param(self, key, value):
        self.params[key].append(value)


class ForceMinSizePass(Pass):
    async def process(self, runner, params, link):
        return await runner.opt(
            link,
            "--passes",
            "remove-function-attr<optnone>,remove-function-attr<noinline>,add-function-attr<minsize>,add-function-attr<optsize>",
        )


class StandardPass(Pass):
    def add_default_params(self, config):
        if config.level >= 10:
            # TODO: in what situations is this actually an improvement?
            # Disabled because we aren't currently building LLVM with
            # TensorFlow support.
            # self.add_param("enable-ml-inliner", "release")

    async def process(self, runner, params, link):
        args = ["-Oz"]
        for key, value in params.items():
            args.append(f"--{key}={value}")
        return await runner.opt(link, *args)


class SmoutPass(Pass):
    def add_default_params(self, config):
        self.add_param("compile_all_callers", config.level >= 9)
        if config.level >= 8:
            self.add_param("max_nodes", 200)
            self.add_param("max_args", 10)
            self.add_param("min_rough_caller_savings", 1)
        else:
            self.add_param("max_nodes", 50)
            self.add_param("max_args", 5)
            self.add_param("min_rough_caller_savings", 5)
        self.add_param("max_adjacent", 10)
        self.add_param("min_benefit", 1)
        self.add_param("min_caller_savings", 1)
        self.add_param("verify_caller_savings", True)
        self.add_param("use_alive2", False)

    async def process(self, runner, params, link):
        # Smout doesn't rewrite debug info when it extracts functions, so we
        # need to strip all debug info to prevent assertion failures.
        #
        # LLVM value names can prevent smout from finding duplicate code, so we
        # eliminate them.
        link = await runner.opt(link, "--strip-debug", "--discard-value-names")

        options = Link(
            runner.store,
            node=params,
        )
        call = Call("smout.optimized_v6", (await options.as_cid(), await link.as_cid()))
        link = await runner.store.evaluate(call)

        link = await runner.opt(
            link, "--passes", "function(simplifycfg),function-attrs"
        )
        return link


class CompilePass(Pass):
    def add_default_params(self, config):
        if config.level == 0:
            self.add_param("O", 0)
        else:
            self.add_param("O", 2)

        self.add_param("cost-kind", "code-size")

        if config.level >= 3:
            # 5 rounds total, same as Chabbi paper.
            self.add_param("machine-outliner", 5)

    async def process(self, runner, params, link):
        args = await runner.cmd(
            "bc-imitate", "llc-args", "-", input=link, input_fmt="bc", output_fmt="txt"
        )
        args = await args.as_node()
        args = [arg for arg in args.split("\n") if arg]
        args.append("-filetype=obj")
        for key, value in params.items():
            if key == "machine-outliner":
                if value:
                    args.append("--enable-machine-outliner=always")
                    args.append(f"--machine-outliner-reruns={value-1}")
                else:
                    args.append("--enable-machine-outliner=never")
            else:
                args.append(f"--{key}={value}")

        return await runner.cmd(
            "llc", *args, input=link, input_fmt="bc", output_fmt="o"
        )
