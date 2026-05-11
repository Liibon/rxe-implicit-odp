# Claims

## Implemented

- Local-access implicit ODP MR registration for RXE.
- `addr == NULL`, `length == SIZE_MAX`, `IBV_ACCESS_ON_DEMAND` accepted.
- Returned lkey usable for local SGE access.
- Verbs tests for registration accept/reject behavior.
- Registration-latency benchmark across 4KB to 1GB.

## Not implemented

- Remote implicit rkey access.
- `IBV_ACCESS_REMOTE_WRITE`, `IBV_ACCESS_REMOTE_READ`, `IBV_ACCESS_REMOTE_ATOMIC` on implicit MRs.
- Upstream submission.
- Hardware RDMA performance comparison.

## Why

Explicit MR registration pins and maps pages at registration time, so cost
scales with region size. Implicit ODP defers page resolution to first access,
which lets registration of a full-address-space-shaped MR stay near constant.
