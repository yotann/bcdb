#! /usr/bin/env nix-shell
#! nix-shell -i python3 -p linuxPackages.perf python3

import csv
import glob
import json
import os
import os.path
import re
import statistics
import subprocess
import sys

rows = []
for dn in sys.argv[1:]:
    if not os.path.exists(f"{dn}/pyperf.json"):
        continue
    row = {}
    path = dn.split("/")
    row["configuration"] = path[-2]
    row["benchmark"] = path[-1]

    dl_startup = []
    dl_reloc = []
    dl_load = []
    for ld in glob.glob(f"{dn}/ld-statistics*"):
        d = open(ld, encoding="cp437").read()
        for x in re.findall(r"total startup time in dynamic loader: (\d+)", d):
            dl_startup.append(int(x))
        for x in re.findall(r"time needed for relocation: (\d+)", d):
            dl_reloc.append(int(x))
        for x in re.findall(r"time needed to load objects: (\d+)", d):
            dl_load.append(int(x))
    row["dl_startup"] = statistics.mean(dl_startup)
    row["dl_reloc"] = statistics.mean(dl_reloc)
    row["dl_load"] = statistics.mean(dl_load)

    if os.path.exists(f"{dn}/perf.data"):
        p = subprocess.run(
            ("perf", "stat", "report", "-i", f"{dn}/perf.data"),
            capture_output=True,
            universal_newlines=True,
        )
        for v, k in re.findall(r"^([ 0-9,.]{18}) .... ([^ ]+)", p.stderr, re.M):
            v = float(v.replace(",", ""))
            row[k] = v

    with open(f"{dn}/pyperf.json") as f:
        j = json.load(f)
        assert j["metadata"]["unit"] == "second"
        for j in j["benchmarks"]:
            row = dict(row)
            if "metadata" in j:
                row["benchmark"] = j["metadata"]["name"]

            time = []
            rss = []
            for run in j["runs"]:
                if run["metadata"].get("calibrate_loops", False):
                    continue
                time.extend(run["values"])
                if "mem_max_rss" in run["metadata"]:
                    rss.append(run["metadata"]["mem_max_rss"])
                elif "command_max_rss" in run["metadata"]:
                    rss.append(run["metadata"]["command_max_rss"])
                else:
                    assert False, run

            row["time_mean"] = statistics.mean(time)
            row["time_stdev"] = statistics.stdev(time)
            row["rss_mean"] = statistics.mean(rss)
            row["rss_stdev"] = statistics.stdev(rss)
            rows.append(row)


def baseline_key(row):
    return row["benchmark"]


baseline = {}
for row in rows:
    if row["configuration"] == "lto":
        baseline[baseline_key(row)] = row

for row in rows:
    bl = baseline.get(baseline_key(row), None)
    if not bl:
        continue
    row["speedup"] = bl["time_mean"] / row["time_mean"]
    row["speedup_err"] = (
        (bl["time_stdev"] / bl["time_mean"]) ** 2
        + (row["time_stdev"] / row["time_mean"]) ** 2
    ) ** 0.5 * row["speedup"]

field_names = []
for row in rows:
    for k in row:
        if k not in field_names:
            field_names.append(k)

writer = csv.DictWriter(sys.stdout, fieldnames=field_names)
writer.writeheader()
writer.writerows(rows)
