#!/usr/bin/env python3
# I plot median latency per (mode,size) on log-log axes.
import csv
import sys
import statistics
from collections import defaultdict
import matplotlib.pyplot as plt

if len(sys.argv) < 2:
    print("usage: plot_reg_latency.py reg_latency.csv [out.png]", file=sys.stderr)
    sys.exit(2)
csv_path = sys.argv[1]
out_path = sys.argv[2] if len(sys.argv) > 2 else "reg_latency.png"

buckets = defaultdict(list)
with open(csv_path) as f:
    for row in csv.DictReader(f):
        buckets[(row["mode"], int(row["size_bytes"]))].append(int(row["latency_ns"]))

modes = sorted({m for m, _ in buckets})
fig, ax = plt.subplots(figsize=(7, 4.5))
for mode in modes:
    sizes = sorted({s for m, s in buckets if m == mode})
    med = [statistics.median(buckets[(mode, s)]) for s in sizes]
    ax.plot(sizes, med, marker="o", label=mode)

ax.set_xscale("log")
ax.set_yscale("log")
ax.set_xlabel("MR size (bytes)")
ax.set_ylabel("registration latency (ns, median)")
ax.set_title("MR registration latency: explicit vs implicit ODP")
ax.grid(True, which="both", linestyle=":", alpha=0.6)
ax.legend()
fig.tight_layout()
fig.savefig(out_path, dpi=140)
print(f"wrote {out_path}")
