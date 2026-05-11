/* Same as bench_reg_latency.c but with MADV_NOHUGEPAGE on the explicit
 * buffer. I want to know if THP collapsing is what flattens the
 * 1MiB..64MiB curve in the THP-enabled run. If the plateau disappears
 * here and we get monotonic scaling, THP is the cause. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <infiniband/verbs.h>

static const size_t sizes[] = {
	4ull * 1024,
	64ull * 1024,
	1ull * 1024 * 1024,
	16ull * 1024 * 1024,
	64ull * 1024 * 1024,
	256ull * 1024 * 1024,
};
#define N_SIZES (sizeof(sizes)/sizeof(sizes[0]))
#define ITERS 5

static uint64_t ns_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static struct ibv_context *open_first(void)
{
	int n = 0;
	struct ibv_device **list = ibv_get_device_list(&n);
	if (!list || n == 0) return NULL;
	struct ibv_context *ctx = ibv_open_device(list[0]);
	ibv_free_device_list(list);
	return ctx;
}

static void bench_explicit_nothp(struct ibv_pd *pd, size_t size, int iter)
{
	void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) { perror("mmap"); return; }

	/* Force 4 KiB pages: disable THP for this VMA before touching. */
	if (madvise(buf, size, MADV_NOHUGEPAGE) < 0)
		perror("madvise(NOHUGEPAGE)");

	memset(buf, 1, size);

	uint64_t t0 = ns_now();
	struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
	uint64_t t1 = ns_now();

	if (mr) {
		printf("explicit_nothp,%zu,%d,%lu\n", size, iter,
			(unsigned long)(t1 - t0));
		ibv_dereg_mr(mr);
	} else {
		fprintf(stderr, "nothp explicit reg failed at size %zu: %s\n",
			size, strerror(errno));
	}
	munmap(buf, size);
}

int main(void)
{
	struct ibv_context *ctx = open_first();
	if (!ctx) { fprintf(stderr, "no device\n"); return 77; }
	struct ibv_pd *pd = ibv_alloc_pd(ctx);
	if (!pd) { perror("alloc_pd"); return 1; }

	printf("mode,size_bytes,iter,latency_ns\n");
	for (size_t i = 0; i < N_SIZES; i++)
		for (int it = 0; it < ITERS; it++)
			bench_explicit_nothp(pd, sizes[i], it);

	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	return 0;
}
