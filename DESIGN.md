# Design

## Background

RXE is the Linux Soft-RoCE driver. It exposes RDMA semantics over an
Ethernet device without RDMA-capable NIC hardware. MR registration pins
user pages and records the mapping under an lkey/rkey for later verbs
operations.

ODP defers page resolution: instead of pinning at registration, pages
are faulted in on access using the `mmu_interval_notifier` path that
the existing RXE explicit ODP support already wires up.

## Scope

Add the implicit ODP form (`addr == 0`, `length == U64_MAX`,
`IB_ACCESS_ON_DEMAND`) restricted to local access. Remote rkey use on
an implicit MR is rejected with `-EOPNOTSUPP` at registration time.

## Touch points

- `drivers/infiniband/sw/rxe/rxe.c` advertises `IB_ODP_SUPPORT_IMPLICIT`.
- `drivers/infiniband/sw/rxe/rxe_verbs.h` carries the chunk-size
  constants and a `struct xarray implicit_children` on `struct rxe_mr`.
- `drivers/infiniband/sw/rxe/rxe_odp.c` plumbs the registration path
  through `ib_umem_odp_alloc_implicit()` and routes the SGE walker
  through per-chunk child umems.
- `drivers/infiniband/sw/rxe/rxe_mr.c` releases every child during MR
  cleanup before releasing the parent.

## Registration path

`rxe_reg_user_mr` already routes `IB_ACCESS_ON_DEMAND` to
`rxe_odp_mr_init_user`. The new branch detects implicit form
(`start == 0 && length == U64_MAX`) and calls
`rxe_odp_mr_init_implicit`. That helper:

1. Rejects any remote-access bit with `-EOPNOTSUPP`.
2. Calls `ib_umem_odp_alloc_implicit()` to build the empty parent umem
   that serves as an anchor for children and as the source of the
   per-mm reference.
3. Sets `mr->umem`, `mr->access`, `mr->ibmr.length = U64_MAX`.
4. Initializes `mr->implicit_children` via `xa_init()`.
5. Skips `rxe_odp_init_pages()`: there is nothing to fault at
   registration time. The lkey is valid immediately.

## Chunking

Implicit MRs split the virtual address space into fixed-size chunks
sized by `RXE_ODP_CHILD_SHIFT`:

```c
#define RXE_ODP_CHILD_SHIFT 21
#define RXE_ODP_CHILD_SIZE  (1UL << RXE_ODP_CHILD_SHIFT)  /* 2 MiB */
#define RXE_ODP_CHILD_MASK  (RXE_ODP_CHILD_SIZE - 1)
```

2 MiB matches the typical THP size on aarch64 and x86_64, so a single
chunk can be backed by a huge page when one is available. Each chunk
is represented by exactly one child `ib_umem_odp` allocated lazily.

## Fault path

`rxe_odp_map_range_and_lock` is the chokepoint for SGE walks on ODP
MRs. The refactor:

1. `rxe_odp_umem_for_iova(mr, iova)` returns:
   - `to_ib_umem_odp(mr->umem)` for explicit MRs.
   - `rxe_odp_get_child(mr, iova)` for implicit MRs.
2. `rxe_odp_get_child` aligns `iova` down to the chunk boundary,
   computes the xarray key as `aligned_start >> RXE_ODP_CHILD_SHIFT`,
   tries `xa_load`. If absent it allocates via
   `ib_umem_odp_alloc_child(parent, aligned_start, RXE_ODP_CHILD_SIZE, &rxe_mn_ops)`,
   then publishes via `xa_cmpxchg(GFP_KERNEL)`. A racing insertion
   makes the loser release its child and use the winner's.
3. `rxe_odp_chunk_len_at(mr, iova, length)` returns the longest run of
   bytes serviceable by a single umem starting at `iova`: full length
   for explicit MRs, distance to the next chunk boundary for implicit.
4. `rxe_odp_mr_copy` runs a loop over chunks; each iteration calls
   `map_range_and_lock` with a clamped length, then
   `__rxe_odp_mr_copy_one` to copy this chunk's bytes, then
   `mutex_unlock`. For explicit MRs the loop runs exactly once and
   behavior is identical to pre-patch.

This keeps the per-umem helpers (`ib_umem_odp_map_dma_and_lock`,
`rxe_check_pagefault`, the kmap-based page walker) unchanged. They
operate on whatever `umem_odp` the caller chose.

## Prefetch path

`ib_advise_mr` prefetch reuses `rxe_odp_umem_for_iova` and the same
chunk loop. Async prefetch (queued work) walks the SGE list once,
takes references to each `rxe_mr`, and faults each chunk under its own
short-held mutex so a long range does not block concurrent
invalidators across the whole MR.

## Atomic, flush, atomic_write

These paths assume `mr->umem` has a populated `pfn_list`, which is
not true for an implicit parent. The prototype rejects implicit MRs
at the top of each helper rather than extending them, since the test
matrix does not exercise these and the rejection keeps the public
claim aligned with what is verified.

## Lifetime

- The parent umem is released by `ib_umem_release(mr->umem)` in
  `rxe_mr_cleanup` (existing path; the `is_odp` flag routes to
  `ib_umem_odp_release` internally for ODP umems).
- Children are released by walking `mr->implicit_children` with
  `xa_for_each` and calling `ib_umem_odp_release` on each, then
  `xa_destroy`. This happens before releasing the parent so each
  child's `mmu_interval_notifier` tears down while the parent's
  per_mm is still alive.

## Limitations

- Monotonic growth: a child once allocated stays until MR destroy.
  Long-lived MRs that touch a sparse address space would accumulate
  children. A real implementation would add a reclaim mechanism
  triggered by memory pressure or notifier invalidations.
- No explicit serialization around the xarray beyond the
  `xa_cmpxchg`'s atomicity. Concurrent SGE walks targeting the same
  newly-touched chunk race correctly on creation but then proceed
  independently; the `umem_mutex` on the chosen child serializes
  page-fault / DMA-map updates as in the explicit path.
