# RXE local-access implicit ODP

Prototype local-access implicit On-Demand Paging for Linux Soft-RoCE/RXE.

Implemented registration form:

```c
ibv_reg_mr(pd, NULL, SIZE_MAX,
           IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE);
```

The returned lkey is usable for local SGE access. Remote implicit access is out
of scope for this prototype.

## Layout

- `patches/` kernel patch against `drivers/infiniband/sw/rxe/`
- `tests/` libibverbs validation programs
- `bench/` registration-latency benchmark
- `results/` measured output and system info

## Kernel branch

[Liibon/linux-rxe-odp:rxe-local-implicit-odp](https://github.com/Liibon/linux-rxe-odp/tree/rxe-local-implicit-odp)

## Reproduce

1. Build patched kernel from the kernel branch above.
2. Boot it.
3. `modprobe rdma_rxe` and bring up an rxe device.
4. `make -C tests && make -C bench`
5. `bench/run.sh`
