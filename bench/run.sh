#!/bin/bash
# I run the registration-latency bench and dump CSV to results/.
# Caller must have brought up an rxe device already.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
outdir="${1:-$repo/results/linux-6.x}"
mkdir -p "$outdir"

cd "$here"
make >/dev/null

# I do a warmup pass to settle caches and module init. The first registration
# after modprobe is consistently slow and not representative.
./bench_reg_latency >/dev/null

./bench_reg_latency > "$outdir/reg_latency.csv"
echo "wrote $outdir/reg_latency.csv"
