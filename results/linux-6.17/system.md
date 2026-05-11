# System

| field | value |
|---|---|
| kernel | 6.17.0-rxe-implicit-odp-00001-g4227171aab16 |
| base | torvalds/linux tag v6.17 (commit `e5f0a698b`) |
| arch | aarch64 |
| host | macOS lima VM (Apple Virtualization framework) |
| distro | Ubuntu 24.04.4 LTS |
| cpus | 4 |
| memory | 5.8Gi |
| rdma-core | 1.14.50.0 |

## ODP caps reported by the patched device

```
	general_odp_caps:
					ODP_SUPPORT
					ODP_SUPPORT_IMPLICIT
	rc_odp_caps:
```

## Tests

- tests/implicit_odp_reg_test: 5/5 cases pass.
- tests/implicit_odp_write_test: passes. 64 KiB RDMA WRITE delivered using
  an implicit ODP local lkey on a same-device RC loopback. The source
  buffer is plain anonymous mmap memory faulted in on first SGE access
  through a lazily allocated child umem.

## Bench notes

- The 1 GiB explicit row fails to allocate inside the 6 GiB VM. That is
  itself the property in question: explicit registration needs the
  backing memory available at registration time; implicit does not.
- Each (mode, size) measured ITERS=5 times after a warmup pass.
- Latency measured around ibv_reg_mr only.
