# Design

## Background

RXE is the Linux Soft-RoCE driver. It exposes RDMA semantics over an
Ethernet device without RDMA-capable NIC hardware. MR registration pins
user pages and records the mapping under an lkey/rkey for later verbs
operations.

ODP defers page resolution: instead of pinning at registration, pages
are faulted in on access using the `mmu_interval_notifier` path that the
existing RXE explicit ODP support already wires up.

## Scope

Add the implicit ODP form (`addr == 0`, `length == U64_MAX`,
`IB_ACCESS_ON_DEMAND`) restricted to local access. Remote rkey use on an
implicit MR is rejected with `-EOPNOTSUPP` at registration time.

## Touch points

- `drivers/infiniband/sw/rxe/rxe.c` advertises `IB_ODP_SUPPORT_IMPLICIT`.
- `drivers/infiniband/sw/rxe/rxe_verbs.h` carries a single-child cache
  on `struct rxe_mr` for implicit MRs.
- `drivers/infiniband/sw/rxe/rxe_odp.c` plumbs the registration path
  through `ib_umem_odp_alloc_implicit()` and routes the SGE walker
  through child umems.
- `drivers/infiniband/sw/rxe/rxe_mr.c` releases the child during MR
  cleanup before releasing the parent.

## Registration path

`rxe_reg_user_mr` already routes `IB_ACCESS_ON_DEMAND` to
`rxe_odp_mr_init_user`. The new branch detects implicit form
(`start == 0 && length == U64_MAX`) and calls a new helper
`rxe_odp_mr_init_implicit`. That helper:

1. Rejects any remote-access bit with `-EOPNOTSUPP`.
2. Calls `ib_umem_odp_alloc_implicit()` to build the empty parent umem
   that serves as an anchor for children and as the source of the
   per-mm reference.
3. Sets `mr->umem`, `mr->access`, `mr->ibmr.length = U64_MAX`,
   `mr->odp_child = NULL`.
4. Skips `rxe_odp_init_pages()`: there is nothing to fault in at
   registration time. The lkey is valid immediately.

## Fault path

`rxe_odp_map_range_and_lock` is the chokepoint for all SGE walks on ODP
MRs. The refactor:

1. New helper `rxe_odp_umem_for_iova(mr, iova, length)` returns:
   - `to_ib_umem_odp(mr->umem)` for explicit MRs (`is_implicit_odp == false`).
   - `rxe_odp_resolve_implicit(mr, iova, length)` for implicit MRs.
2. `rxe_odp_resolve_implicit`:
   - If `mr->odp_child` covers `[iova, iova+length)` (page-aligned),
     reuse it.
   - Otherwise release the old child and allocate a fresh one via
     `ib_umem_odp_alloc_child(parent, aligned_start, size, &rxe_mn_ops)`.
3. `rxe_odp_map_range_and_lock` returns the chosen `umem_odp` to the
   caller via an out parameter; all downstream helpers
   (`__rxe_odp_mr_copy`, `rxe_odp_do_pagefault_and_lock`) take the
   `umem_odp` directly instead of looking it up from `mr->umem`.

This keeps the existing explicit-MR behavior identical: the helper just
returns the same parent umem that the previous code derived locally.
For implicit MRs the same helpers now see a child umem with a real
page-fault and pfn_list interface, so `ib_umem_odp_map_dma_and_lock`,
`rxe_check_pagefault`, and the kmap-based copy loop work unchanged.

## Prefetch path

`ib_advise_mr` prefetch is routed through the same
`rxe_odp_umem_for_iova` helper so an implicit MR can be prefetched as a
single SGE range. Same semantics: a child is created and faulted, then
the lock is released. Asynchronous prefetch goes through a workqueue,
and child creation happens there.

## Atomic, flush, atomic_write

These paths assume `mr->umem` has a populated `pfn_list`, which is not
true for an implicit parent. The prototype rejects implicit MRs at the
top of each helper rather than extending them, since the test matrix
does not exercise these and the rejection keeps the public claim aligned
with what is verified.

## Lifetime

- The parent umem is released by `ib_umem_release(mr->umem)` in
  `rxe_mr_cleanup` (existing path; the `is_odp` flag routes to
  `ib_umem_odp_release` internally for ODP umems).
- The child umem is released by an explicit
  `ib_umem_odp_release(mr->odp_child)` in `rxe_mr_cleanup` before the
  parent release, so the child's mmu_interval_notifier tears down while
  the parent's per_mm is still alive.

## Limitations

Single-child cache. Production code would maintain an xarray keyed by
aligned start. The prototype keeps one child per MR which is sufficient
for the test workload (one buffer, one access range) but would thrash on
disjoint multi-range workloads.

No concurrency protection on `mr->odp_child`. The cache is mutated under
the implicit assumption that SGE walks for the same MR are serialized at
a higher level. This is true for the test, not generally.
