# Claims

## Implemented and verified

- Local-access implicit ODP MR registration for RXE.
  - `addr == 0`, `length == U64_MAX`, `IB_ACCESS_ON_DEMAND` accepted.
  - `ib_umem_odp_alloc_implicit()` wired into the existing `rxe_odp_mr_init_user` path.
  - Verified by `tests/implicit_odp_reg_test` (5/5 pass on the patched kernel).
- `IB_ODP_SUPPORT_IMPLICIT` advertised in `general_odp_caps` (visible in `ibv_devinfo -v`).
- Remote-access rejection on implicit MRs:
  - `IB_ACCESS_REMOTE_READ`, `IB_ACCESS_REMOTE_WRITE`, `IB_ACCESS_REMOTE_ATOMIC`
    return `-EOPNOTSUPP` at registration time.
- Registration-latency measurement on the patched kernel for 4 KiB to 1 GiB.
  - Median latencies in `results/linux-6.17/reg_latency.csv`.
  - Explicit MR registration scales roughly linearly with region size and
    fails to allocate at 1 GiB in a 6 GiB VM.
  - Implicit MR registration stays under ~15 microseconds across the entire
    range, including 1 GiB.

## Not implemented

- Local SGE fault path for implicit MRs.
  - The lkey is valid at the registration boundary, but the SGE walk does
    not yet create child umems via `ib_umem_odp_get()` on first access.
  - As a result `tests/implicit_odp_write_test` hangs in `ibv_poll_cq()`:
    the work request is posted, no fault path resolves the source page,
    and no completion arrives.
  - This is the natural next chunk of work and is documented in DESIGN.md
    under "Fault path".
- Remote rkey access on implicit MRs (intentionally out of scope).
- Upstream submission.
- Hardware RDMA comparison.

## Why this matters

Explicit MR registration pins and maps user pages at registration time, so
the registration call has to do work proportional to the region size and
needs the memory to be available. Implicit ODP defers page resolution to
first access, so registration cost is bounded by the constant work to set
up the umem and notifier. The measured graph is the literal restatement of
that property on the Soft-RoCE driver.
