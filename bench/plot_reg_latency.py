#!/usr/bin/env python3
# Two-panel plot from the aggregated CSV.
# Top: registration latency median with p95/p99 shading.
# Bottom: process RSS during a registered MR window. The implicit line
# stays flat against the explicit line that grows with size, which is the
# property the patch is trying to demonstrate.

import csv
import sys
from collections import defaultdict
import matplotlib.pyplot as plt

if len(sys.argv) < 2:
    print("usage: plot_reg_latency.py reg_latency.csv [out.png]", file=sys.stderr)
    sys.exit(2)
csv_path = sys.argv[1]
out_path = sys.argv[2] if len(sys.argv) > 2 else "reg_latency.png"

rows = []
with open(csv_path) as f:
    for r in csv.DictReader(f):
        rows.append(r)

modes = sorted({r["mode"] for r in rows})

# Latency series: only rows with at least one successful sample.
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)

colors = {"explicit": "#1f77b4", "implicit": "#ff7f0e"}

for mode in modes:
    s = sorted(
        (int(r["requested_size"]), int(r["median_ns"]),
         int(r["p95_ns"]), int(r["p99_ns"]),
         int(r["fail_count"]), int(r["rss_after_kb"]))
        for r in rows if r["mode"] == mode
    )
    sizes_ok    = [x[0] for x in s if x[4] == 0]
    median_ok   = [x[1] for x in s if x[4] == 0]
    p95_ok      = [x[2] for x in s if x[4] == 0]
    p99_ok      = [x[3] for x in s if x[4] == 0]
    sizes_fail  = [x[0] for x in s if x[4] > 0]

    if sizes_ok:
        ax1.plot(sizes_ok, median_ok, marker="o", color=colors[mode],
                 label=f"{mode} (median)")
        ax1.fill_between(sizes_ok, median_ok, p95_ok, color=colors[mode],
                         alpha=0.18, label=f"{mode} median..p95")
        ax1.plot(sizes_ok, p99_ok, color=colors[mode], linestyle=":",
                 linewidth=1.0, label=f"{mode} p99")
    # Mark failed buckets with an x.
    for sz in sizes_fail:
        ax1.scatter([sz], [1], marker="x", color=colors[mode], s=60,
                    label=f"{mode} ENOMEM" if sz == sizes_fail[0] else None)

    sizes_all = [x[0] for x in s]
    rss_all   = [x[5] for x in s]
    ax2.plot(sizes_all, rss_all, marker="o", color=colors[mode], label=mode)

ax1.set_xscale("log")
ax1.set_yscale("log")
ax1.set_ylabel("ibv_reg_mr latency (ns)")
ax1.set_title("MR registration latency: explicit vs implicit ODP (n=30, shuffled)")
ax1.grid(True, which="both", linestyle=":", alpha=0.6)
ax1.legend(fontsize=8, loc="upper left")

ax2.set_xscale("log")
ax2.set_yscale("log")
ax2.set_xlabel("requested_size (bytes)")
ax2.set_ylabel("RSS during MR held (kB)")
ax2.set_title("Process RSS while one MR of this size is registered")
ax2.grid(True, which="both", linestyle=":", alpha=0.6)
ax2.legend(fontsize=9)

fig.tight_layout()
fig.savefig(out_path, dpi=140)
print(f"wrote {out_path}")
