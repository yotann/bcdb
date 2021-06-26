import concurrent.futures
import glob
import os
import os.path
import pyperf
import queue
import re
import shlex
import subprocess


def choose_perf_events():
    perf_events = ["cpu-clock:u", "cpu-clock:k", "task-clock:u", "task-clock:k"]
    perf_events += ["context-switches", "cpu-migrations", "page-faults"]
    perf_events += [
        "cycles:u",
        "cpu-cycles:u",
        "stalled-cycles-frontend:u",
        "stalled-cycles-backend:u",
        "instructions:u",
    ]
    perf_events += ["branch-load-misses:u", "branch-loads:u"]
    perf_events += ["LLC-loads:u", "LLC-load-misses:u"]
    perf_events += ["cache-misses:u", "cache-references:u"]
    p = subprocess.run(
        ("perf", "list", "--no-desc"), capture_output=True, universal_newlines=True
    )
    for e in re.findall(r"^  (\S+) .*\[.* event\]$", p.stdout, re.M):
        if "itlb" in e.lower() or "icache" in e.lower() or "l1i" in e.lower():
            perf_events.append(e + ":u")
    return perf_events


def run_benchmarks(
    generate_args_env,
    exe_paths,
    benchmarks,
    output_dir,
    cpus_per_task=1,
    enable_perf=False,
):
    perf_events = choose_perf_events() if enable_perf else []

    cpus = pyperf._cpu_utils.get_isolated_cpus()
    if not cpus:
        print("WARNING: no isolated CPUs detected, will use all CPUs")
        cpus = tuple(range(pyperf._cpu_utils.get_logical_cpu_count()))

    cpu_queue = queue.SimpleQueue()

    for i in range(0, len(cpus), cpus_per_task):
        task_cpus = cpus[i : i + cpus_per_task]
        if len(task_cpus) == cpus_per_task:
            cpu_queue.put(task_cpus)

    def runner(job):
        nonlocal cpu_queue

        path, benchmark = job

        path_tail = path
        if "/" in path_tail:
            path_tail = path_tail[path_tail.rindex("/") + 1 :]
        out = f"{output_dir}/{path_tail}/{benchmark}"
        out_pyperf = f"{out}/pyperf.json"
        if os.path.exists(out_pyperf):
            # Already run.
            return
        for fn in glob.glob(f"{out}/ld-statistics*"):
            os.remove(fn)

        task_cpus = cpu_queue.get()

        try:
            affinity = pyperf._cpu_utils.format_cpu_list(task_cpus)
            print("running", path, benchmark, "on", affinity)

            path = os.path.abspath(path)
            args, env = generate_args_env(path, benchmark, out, affinity)
            if "PATH" not in env:
                env["PATH"] = f'{path}/bin:{os.environ["PATH"]}'
            if "LD_BIND_NOW" in os.environ:
                env["LD_BIND_NOW"] = os.environ["LD_BIND_NOW"]

            os.makedirs(out, exist_ok=True)

            # XXX: perf adds /run/current-system/sw/bin to PATH
            if perf_events:
                args = [
                    "perf",
                    "stat",
                    "record",
                    "-o",
                    f"{out}/perf.data",
                    "-e",
                    ",".join(perf_events),
                ] + args

            env["LD_DEBUG"] = "statistics,files"
            env["LD_DEBUG_OUTPUT"] = f"{out}/ld-statistics"

            with open(f"{out}/env", "w") as f:
                for k, v in env.items():
                    f.write(f"{k}={v}\n")
            with open(f"{out}/args", "w") as f:
                f.write(shlex.join(args) + "\n")

            with open(f"{out}/stderr.txt", "ab") as stderr:
                r = subprocess.run(args, env=env, stderr=stderr)

        except Exception as e:
            print(e)
            raise

        finally:
            cpu_queue.put(task_cpus)

    with concurrent.futures.ThreadPoolExecutor(cpu_queue.qsize() + 1) as executor:
        executor.map(
            runner,
            [
                (exe_path, benchmark)
                for exe_path in exe_paths
                for benchmark in benchmarks
            ],
        )
