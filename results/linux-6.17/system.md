# System

| field | value |
|---|---|
| kernel | 6.17.0-rxe-implicit-odp-00001-g4227171aab16 |
| base | torvalds/linux tag v6.17 (commit `e5f0a698b`) |
| arch | aarch64 |
| host | macOS lima VM (Apple Virtualization framework) |
| distro | Ubuntu 24.04.4 LTS |
| cpus | 4 |
| memory | 5.8Gi |
| rdma-core | 1.14.50.0 |
| THP | `[always]` (auto-promote anonymous mappings to 2 MiB folios) |
| RXE_ODP_CHILD_SHIFT | 21 (2 MiB chunks) |

## ODP caps reported by the patched device

```
	general_odp_caps:
					ODP_SUPPORT
					ODP_SUPPORT_IMPLICIT
	rc_odp_caps:
```

## Tests

- tests/implicit_odp_reg_test: 5/5 cases pass.
- tests/implicit_odp_write_test: 64 KiB RDMA WRITE through implicit lkey passes.
- tests/implicit_odp_multi_test: two RDMA WRITEs from buffers in different
  2 MiB chunks of one implicit MR, both delivered. Exercises the xarray
  by forcing two distinct child umems to coexist on the same MR.

## Bench

- See `NOTES.md` for the full CSV schema and methodology.
- n=30 samples per (mode, requested_size) bucket, shuffled order, one
  warmup iteration per bucket discarded.
- The 1 GiB explicit row shows `fail_count=30, errno=12 (ENOMEM)` and
  empty latency stats. The bench keeps the row rather than dropping it,
  because that failure is itself the property under test: explicit
  registration cannot proceed without backing memory available, implicit
  registers in microseconds with RSS unchanged.
- `rss_after_kb` in `reg_latency.csv` makes the underlying contrast
  explicit: explicit pins pages so RSS climbs with size, implicit does
  not.
- `reg_latency_nothp.csv` is a companion run with `MADV_NOHUGEPAGE` on
  explicit buffers, showing the same shape with steeper per-byte cost
  when the kernel cannot collapse 2 MiB folios.
