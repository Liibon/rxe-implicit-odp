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
| ibv_devinfo | rxe0 (Soft-RoCE over eth0) |

## ODP caps reported by the patched device

```
	general_odp_caps:
					ODP_SUPPORT
					ODP_SUPPORT_IMPLICIT
	rc_odp_caps:
```

## Notes

- The 1 GiB explicit row reports failure because the bench cannot allocate
  a 1 GiB anonymous mapping in the 6 GiB VM after touch + pin. This is the
  whole point of implicit ODP: registration cost does not require a
  backing allocation.
- Each (mode, size) was measured ITERS=5 times; a warmup pass is discarded.
- Latencies measured around ibv_reg_mr only.
