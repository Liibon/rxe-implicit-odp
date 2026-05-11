/* Registration latency for explicit MR vs implicit ODP MR.
 * I emit CSV on stdout: mode,size_bytes,iter,latency_ns.
 * The implicit row uses NULL/SIZE_MAX so size_bytes there is logical, not
 * physical, since implicit registration does not pin pages. I still report
 * it under size_bytes for plotting symmetry. */

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
	1024ull * 1024 * 1024,
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

static void bench_explicit(struct ibv_pd *pd, size_t size, int iter)
{
	/* I touch every page before registration so the cost of page-fault-in
	 * does not bleed into the registration measurement. That keeps the
	 * comparison honest: explicit cost = pin/map only. */
	void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) { perror("mmap"); return; }
	memset(buf, 1, size);

	uint64_t t0 = ns_now();
	struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
	uint64_t t1 = ns_now();

	if (mr) {
		printf("explicit,%zu,%d,%lu\n", size, iter, (unsigned long)(t1 - t0));
		ibv_dereg_mr(mr);
	} else {
		fprintf(stderr, "explicit reg failed at size %zu: %s\n",
			size, strerror(errno));
	}
	munmap(buf, size);
}

static void bench_implicit(struct ibv_pd *pd, size_t size, int iter)
{
	/* Implicit registration ignores addr/length in the sense that no pages
	 * are pinned. I still take the timestamp around the same call shape so
	 * the comparison is fair. */
	uint64_t t0 = ns_now();
	struct ibv_mr *mr = ibv_reg_mr(pd, NULL, SIZE_MAX,
		IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE);
	uint64_t t1 = ns_now();

	if (mr) {
		printf("implicit,%zu,%d,%lu\n", size, iter, (unsigned long)(t1 - t0));
		ibv_dereg_mr(mr);
	} else {
		fprintf(stderr, "implicit reg failed at size %zu: %s\n",
			size, strerror(errno));
	}
}

int main(int argc, char **argv)
{
	int do_explicit = 1, do_implicit = 1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--explicit-only")) do_implicit = 0;
		else if (!strcmp(argv[i], "--implicit-only")) do_explicit = 0;
	}

	struct ibv_context *ctx = open_first();
	if (!ctx) { fprintf(stderr, "no device\n"); return 77; }
	struct ibv_pd *pd = ibv_alloc_pd(ctx);
	if (!pd) { perror("alloc_pd"); return 1; }

	printf("mode,size_bytes,iter,latency_ns\n");
	for (size_t i = 0; i < N_SIZES; i++) {
		for (int it = 0; it < ITERS; it++) {
			if (do_explicit) bench_explicit(pd, sizes[i], it);
			if (do_implicit) bench_implicit(pd, sizes[i], it);
		}
	}
	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	return 0;
}
