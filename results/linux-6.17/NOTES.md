# Bench methodology

## CSV schema (reg_latency.csv)

One row per (mode, requested_size) bucket.

| column | meaning |
|---|---|
| mode | `explicit` or `implicit` |
| requested_size | the size label used to pair this row against the matching row of the other mode for comparison. For implicit it does **not** describe the size of the registered MR; the implicit MR is always the full address space. |
| actual_addr_arg | the `addr` argument actually passed to `ibv_reg_mr`. `non-null` for explicit, `NULL` for implicit. |
| actual_length_arg | the `length` argument actually passed. `requested_size` bytes for explicit, `SIZE_MAX` for implicit. |
| access_flags | the access flags passed. `LOCAL_WRITE` for explicit, `ON_DEMAND \| LOCAL_WRITE` for implicit. |
| median_ns | median latency over successful iterations. |
| p95_ns / p99_ns | 95th / 99th percentile latencies. |
| min_ns / max_ns | bounds of the sample. |
| fail_count | iterations that returned an error from `ibv_reg_mr` or `mmap`. |
| errno | the most recent errno seen for a failure (0 if none). |
| rss_before_kb | process VmRSS at startup before any iteration ran. |
| rss_after_kb | peak VmRSS observed while an MR for this bucket was registered. |

## Procedure

- `N_ITERS = 30` measured samples per bucket.
- One warmup iteration per bucket is run before measurement and discarded.
- The plan of `(case, size, iter)` triples is built once and Fisher-Yates shuffled, so size buckets are interleaved across the run. This averages cache and memory-pressure state across buckets rather than favoring the bucket that ran first.
- Each iteration is self-contained: fresh `mmap` + `memset` + `ibv_reg_mr` + RSS sample + `ibv_dereg_mr` + `munmap` for explicit, and `ibv_reg_mr(NULL, SIZE_MAX, ...)` + RSS sample + `ibv_dereg_mr` for implicit.
- RSS is read from `/proc/self/status` (`VmRSS`).
- Failed iterations record the errno and do not contribute to latency stats. The 1 GiB explicit row has `fail_count = 30, errno = 12 (ENOMEM)` and zero stats because every iteration failed to register; this is the row that demonstrates the structural cost of explicit pinning in a memory-constrained environment.

## What to read from the numbers

- The latency curve shows registration time is bounded by setup work for implicit (~1-3 us median, tens of us tail) and grows with region size for explicit (median ~6 us at 4 KiB to ~850 us at 256 MiB).
- The RSS column shows the underlying reason: explicit registration forces the registered region into resident memory (256 MiB MR raises RSS by ~260 MiB), implicit registration does not (RSS stays at the ~3 MB baseline regardless of bucket label).
- 1 GiB explicit cannot register at all in this 6 GiB VM. Implicit registers in microseconds with RSS unchanged.

## Knobs

- THP is `[always]` on this kernel. A companion run with `MADV_NOHUGEPAGE` is in `reg_latency_nothp.csv` for the curious; it shows the same shape of explicit cost with the per-page work scaled up because the kernel cannot collapse huge folios.
