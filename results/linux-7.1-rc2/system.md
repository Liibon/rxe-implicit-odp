# System

| field | value |
|---|---|
| kernel | 7.1.0-rc2-rxe-implicit-odp-fornext-g302ec55bf39a |
| base | `rdma/for-next` at `7fd2df204f34` (Linux 7.1-rc2) |
| arch | aarch64 |
| host | macOS lima VM (Apple Virtualization framework) |
| distro | Ubuntu 24.04.4 LTS |
| cpus | 4 |
| memory | 5.8 GiB |
| rdma-core | 1.14.50.0 |
| THP | `[always]` |
| RXE_ODP_CHILD_SHIFT | 21 (2 MiB chunks) |

## ODP caps reported by the patched device

```
general_odp_caps:
        ODP_SUPPORT
        ODP_SUPPORT_IMPLICIT
```

## Tests on for-next 7.1-rc2

- `tests/implicit_odp_reg_test`: 5/5 pass.
- `tests/implicit_odp_write_test`: 64 KiB RDMA WRITE through implicit lkey OK.
- `tests/implicit_odp_multi_test`: 2 x 1 MiB WRITEs from buffers in different
  2 MiB chunks of one implicit MR OK.
- `tests/implicit_odp_cross_test`: 128 KiB WRITE whose single SGE straddles
  a 2 MiB chunk boundary OK.

## Bench

- `n=30` samples per (mode, requested_size) bucket, shuffled order, one
  warmup iteration per bucket discarded.
- Seed recorded in the CSV header: `# bench_reg_latency seed=42 n_iters=30 ...`.
- Measures registration latency and peak RSS only. First-touch and
  steady-state data path costs are not characterized.

## Rebase notes

The 6.17-shaped patch needed two adjustments to apply cleanly on
7.1-rc2:

- `rxe_verbs.h` 3-way merge: kept the new `struct rxe_mr_page`
  introduced by for-next alongside the new `RXE_ODP_CHILD_*` constants.
- `rxe_odp.c` 3-way merge: took for-next's `kvzalloc_flex()` over the
  v6.17 `kvzalloc(struct_size(...))` form.
- `rxe_odp.c` post-merge: removed `mr->page_offset = 0` from
  `rxe_odp_mr_init_implicit()`; for-next removed the `page_offset`
  field from `struct rxe_mr`. The corresponding assignment in the
  explicit path was already dropped by the 3-way merge because it was
  textually identical to a line for-next had removed.
- `rxe_mr_cleanup`: the v6.17 line `xa_destroy(&mr->page_list)` became
  `free_mr_page_info(mr)` in for-next; 3-way merge folded that in.
