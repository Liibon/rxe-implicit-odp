# Claims

## Implemented and verified

- Local-access implicit ODP MR registration for RXE.
  - `addr == 0`, `length == U64_MAX`, `IB_ACCESS_ON_DEMAND` accepted.
  - `ib_umem_odp_alloc_implicit()` wired into the existing `rxe_odp_mr_init_user` path.
  - Verified by `tests/implicit_odp_reg_test` (5/5 pass on the patched kernel).
- `IB_ODP_SUPPORT_IMPLICIT` advertised in `general_odp_caps`.
- Remote-access rejection on implicit MRs:
  - `IB_ACCESS_REMOTE_READ`, `IB_ACCESS_REMOTE_WRITE`, `IB_ACCESS_REMOTE_ATOMIC`
    return `-EOPNOTSUPP` at registration time.
- Local SGE fault path for implicit MRs.
  - A child umem is created on first access via `ib_umem_odp_alloc_child()`
    and cached on the `struct rxe_mr` (single-child cache).
  - Subsequent SGE accesses to the same range reuse the cached child;
    out-of-range accesses release and reallocate.
  - Page resolution happens via the existing `ib_umem_odp_map_dma_and_lock()`
    against the child umem.
  - Verified by `tests/implicit_odp_write_test`: a 64 KiB RDMA WRITE from
    a plain anonymous mmap buffer is delivered through an implicit ODP
    local lkey on a same-device RC loopback.
  - MMU invalidation continues to work: each child has its own
    `mmu_interval_notifier` registered through `rxe_mn_ops`, and the
    existing invalidate path unmaps DMA pages per range as before.
- Registration-latency measurement on the patched kernel for 4 KiB to 1 GiB.
  - Median latencies in `results/linux-6.17/reg_latency.csv`.
  - Explicit MR registration scales with region size and fails to allocate
    at 1 GiB in a 6 GiB VM.
  - Implicit MR registration stays under ~15 microseconds across the
    entire range, including 1 GiB.

## Not implemented

- Multi-child cache. The prototype keeps one child umem per implicit MR.
  An access that does not fit the cached child releases it and allocates
  a new one. Concurrent SGE accesses to disjoint ranges on the same MR
  would thrash. A production implementation would maintain an xarray of
  children indexed by aligned start address.
- Implicit MR on atomic, flush, and atomic-write paths.
  These return `-EOPNOTSUPP` / `RESPST_ERR_RKEY_VIOLATION` for implicit
  MRs. The atomic and flush helpers walk `mr->umem` directly which is
  the empty implicit parent; extending them would require the same
  child-resolution wrapper used by the copy path.
- Remote rkey access on implicit MRs (intentionally out of scope).
- Upstream submission.
- Hardware RDMA comparison.

## Why this matters

Explicit MR registration pins and maps user pages at registration time,
so the registration call has to do work proportional to the region size
and needs the memory to be available. Implicit ODP defers page resolution
to first access, so registration cost is bounded by the constant work to
set up the umem and notifier. The measured graph is the literal
restatement of that property on the Soft-RoCE driver. The write test is
the proof that the lkey returned at registration is real and useful end
to end.
