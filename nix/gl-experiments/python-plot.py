#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p "python3.withPackages (ps: [ ps.matplotlib ps.pandas ])"
import sys

import matplotlib
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
import pandas as pd
from scipy.stats.mstats import gmean

df = pd.concat([pd.read_csv(arg) for arg in sys.argv[1:]])

pt = pd.pivot_table(
    df, values="time_mean", columns=["configuration"], index=["benchmark"]
)
print(pt["lto"])
pt["speedup"] = pt["lto"] / pt["default"]
pt = pt.sort_values("speedup")
print(pt.head())
print(pt.tail())
print(len(pt))

avg = gmean(pt["speedup"])
print("Geometric mean speedup:", avg)

matplotlib.rcParams["pdf.fonttype"] = 42
matplotlib.rcParams["ps.fonttype"] = 42

pt = pt.sort_values("speedup", ascending=False)
fig, ax = plt.subplots(nrows=1, figsize=(6, 7))
# ax.yaxis.grid(True, color='#aaaaaa', linestyle=':')
ax.axvline(avg, color="#228822", linestyle="--")
if False:
    # NOTE: orientation='horizontal' requires a development version of
    # matplotlib as of 2020-11.
    markerline, stemlines, baseline = ax.stem(
        pt.index,
        pt["speedup"],
        use_line_collection=True,
        bottom=1,
        basefmt="C7-",
        orientation="horizontal",
    )
    markerline.set_zorder(10)
    baseline.set_ydata([0, 1])
    baseline.set_transform(ax.get_xaxis_transform())
    baseline.set_color("black")
    baseline.set_zorder(5)
    ax.set_xlim(0.93, 1.39)
    ax.set_xscale("log")
    ax.tick_params(axis="y", which="major", labelsize=7, rotation=0)
    ax.set_xticks((0.95, 1.0, 1.1, 1.2, 1.3))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, pos: f"{x*100:.0f}%"))
    ax.xaxis.set_minor_formatter(FuncFormatter(lambda x, pos: f"{x*100:.0f}%"))
    ax.set_xlabel("Speed versus LTO+PGO (log scale)")
    ax.annotate(
        f"Geometric mean: {100*avg-100:.1f}% speedup",
        (avg, 35),
        rotation=-90,
        xytext=(3, 14),
        textcoords="offset points",
        color="#228822",
    )
else:
    ax.stem(pt.index, pt["speedup"], use_line_collection=True, bottom=1, basefmt="C7-")
fig.tight_layout()
fig.savefig("results/python-speedup.pdf")
