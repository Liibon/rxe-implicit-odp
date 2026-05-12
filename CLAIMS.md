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
- Local SGE fault path for implicit MRs with chunked child umems.
  - The MR holds an `xarray` of child umems keyed by aligned start, each
    covering one `RXE_ODP_CHILD_SIZE` (2 MiB) chunk.
  - On first access to a chunk the SGE walker calls
    `rxe_odp_get_child(mr, iova)`, which loads the child or allocates a
    new one via `ib_umem_odp_alloc_child()` and inserts it through
    `xa_cmpxchg(GFP_KERNEL)`. Racing inserts drop the loser's child.
  - The SGE copy path loops across chunks: an access that spans more
    than one 2 MiB chunk is split at chunk boundaries, each piece is
    resolved + faulted + copied + unlocked independently.
  - Multiple disjoint ranges on the same implicit MR coexist as
    independent children in the xarray; no thrashing.
  - MMU invalidation works per child: each has its own
    `mmu_interval_notifier` registered through `rxe_mn_ops`.
  - Verified by:
    - `tests/implicit_odp_write_test`: single 64 KiB RDMA WRITE through
      one implicit local lkey.
    - `tests/implicit_odp_multi_test`: two RDMA WRITEs from buffers
      placed in different 2 MiB chunks of the same implicit MR; both
      delivered through one lkey.
- Registration-latency measurement on the patched kernel for 4 KiB to 1 GiB.
  - Median latencies in `results/linux-6.17/reg_latency.csv`.
  - Explicit MR registration scales with region size and fails to allocate
    at 1 GiB in a 6 GiB VM.
  - Implicit MR registration stays under ~15 microseconds across the
    entire range, including 1 GiB.

## Not implemented

- Implicit MR on atomic, flush, and atomic-write paths.
  These return `-EOPNOTSUPP` / `RESPST_ERR_RKEY_VIOLATION` for implicit
  MRs. The atomic and flush helpers walk `mr->umem` directly which is
  the empty implicit parent; extending them would require the same
  child-resolution wrapper used by the copy path.
- Remote rkey access on implicit MRs (intentionally out of scope).
- Eviction of stale children. The xarray grows monotonically per MR
  over its lifetime: once a chunk is touched its child stays until MR
  destroy. Long-lived MRs that walk a sparse address space would
  accumulate children. A real implementation would add a reclaim path.
- Upstream submission.
- Hardware RDMA comparison.

## Why this matters

Explicit MR registration pins and maps user pages at registration time,
so the registration call has to do work proportional to the region size
and needs the memory to be available. Implicit ODP defers page
resolution to first access, so registration cost is bounded by the
constant work to set up the umem and notifier. The benchmark measures
registration-time work only; first-touch and steady-state data path
costs are not characterized here. The write, multi, and cross tests
verify that the returned lkey can be used for local SGE access in the
tested paths.
