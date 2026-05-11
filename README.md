# RXE local-access implicit ODP

Prototype local-access implicit On-Demand Paging for Linux Soft-RoCE/RXE.

Implemented registration form:

```c
ibv_reg_mr(pd, NULL, SIZE_MAX,
           IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE);
```

The lkey is valid at the registration boundary. Remote implicit access is
out of scope for this prototype and is rejected with `-EOPNOTSUPP`.

## Layout

- `patches/` kernel patch against `drivers/infiniband/sw/rxe/`
- `tests/` libibverbs validation programs
- `bench/` registration-latency benchmark
- `results/linux-6.17/` measured output on the patched kernel
- `DESIGN.md` data structures and call paths
- `CLAIMS.md` exact scope of what is and is not implemented

## Result

![registration latency](results/linux-6.17/reg_latency.png)

Implicit registration stays under ~15 microseconds from 4 KiB to 1 GiB.
Explicit registration scales with size and fails to allocate at 1 GiB in
the 6 GiB test VM. See `results/linux-6.17/system.md` for the full system
description and `results/linux-6.17/reg_latency.csv` for the raw data.

## Kernel branch

Patched source: [Liibon/linux-rxe-odp:rxe-local-implicit-odp](https://github.com/Liibon/linux-rxe-odp/tree/rxe-local-implicit-odp)

Base: `torvalds/linux` at tag `v6.17` (commit `e5f0a698b`).

## Reproduce

1. Build and boot the patched kernel from the branch above.
2. `sudo modprobe rdma_rxe`
3. `sudo rdma link add rxe0 type rxe netdev <eth>`
4. `make -C tests && make -C bench`
5. `tests/implicit_odp_reg_test`
6. `bench/run.sh`
