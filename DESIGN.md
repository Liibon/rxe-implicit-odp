# Design

## Background

RXE is the Linux Soft-RoCE driver. It exposes RDMA semantics over an Ethernet
device without RDMA-capable NIC hardware. MR registration pins user pages and
records the mapping under an lkey/rkey for later verbs operations.

ODP defers page resolution: instead of pinning at registration, pages are
faulted in on access using the mmu_interval_notifier path that the existing
RXE explicit ODP support already wires up.

## Scope of this prototype

Only the implicit ODP registration form is added, restricted to local access.
Remote rkey use of implicit MRs is intentionally rejected.

## Touch points in RXE

I expect to touch:

- `rxe_mr.c` MR creation path. I gate the implicit form here and skip
  page-list allocation that explicit MRs perform.
- `rxe_odp.c` fault handling. I extend the lookup so a local SGE walk on an
  implicit MR resolves the user VA range that the access targets.
- `rxe_verbs.c` capability reporting if needed for `IB_DEVICE_ODP_CAPS`
  advertisement of implicit support gated to local access.
- `rxe_mw.c` is untouched. Implicit MWs are not introduced.

## Registration path

I detect the implicit case as `addr == 0 && length == SIZE_MAX &&
(access & IB_ACCESS_ON_DEMAND)`. If any remote access bit is set in this case
I return -EOPNOTSUPP rather than silently dropping the bit.

## Fault path

A local-SGE access on an implicit MR resolves the VA from the work request.
The fault handler hmm_range_fault's that VA range on the registering mm and
maps it into the MR's page list. Subsequent accesses to the same range hit
the cached mapping until invalidation.

## Error handling

I return -EINVAL for the documented invalid combinations and -EOPNOTSUPP for
flag combinations that are valid in the ABI but out of scope for this
prototype. I do not silently accept then fail at first use.

## Test matrix

- accept: implicit + local write
- reject: implicit + no access bits
- reject: implicit + remote write
- reject: implicit + remote read
- reject: implicit + remote atomic
- accept: implicit + local write, then a local SGE write into a fresh page
