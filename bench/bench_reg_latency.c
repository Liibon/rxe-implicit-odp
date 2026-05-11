/* Registration-latency benchmark for explicit MR vs implicit ODP MR.
 *
 * I emit one CSV row per (mode, requested_size) bucket with aggregated
 * stats over N_ITERS samples. The row records the exact args passed to
 * ibv_reg_mr so an implicit row cannot be misread as "I registered a
 * 1 GiB implicit MR": the actual_addr_arg is NULL and the
 * actual_length_arg is SIZE_MAX in every implicit row. requested_size
 * is the size label used to pair the implicit row against the explicit
 * row for comparison.
 *
 * Iteration order across (case, size) is shuffled so cache and memory
 * pressure state averages over buckets instead of favoring the first
 * size scanned.
 *
 * rss_before is the process baseline at startup. rss_after is the peak
 * VmRSS seen while an MR for this bucket was held. For explicit this
 * shows the cost of pinning; for implicit it should not move. */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <infiniband/verbs.h>

#define N_ITERS 30

struct case_def {
	const char *mode;
	const char *access_flags_str;
	int access;
	int is_implicit;
};

static const struct case_def cases[] = {
	{ "explicit", "LOCAL_WRITE", IBV_ACCESS_LOCAL_WRITE, 0 },
	{ "implicit", "ON_DEMAND|LOCAL_WRITE",
	  IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE, 1 },
};
#define N_CASES (int)(sizeof(cases)/sizeof(cases[0]))

static const size_t sizes[] = {
	4ull * 1024,
	64ull * 1024,
	1ull * 1024 * 1024,
	16ull * 1024 * 1024,
	64ull * 1024 * 1024,
	256ull * 1024 * 1024,
	1024ull * 1024 * 1024,
};
#define N_SIZES (int)(sizeof(sizes)/sizeof(sizes[0]))

static uint64_t ns_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int read_rss_kb(void)
{
	FILE *f = fopen("/proc/self/status", "r");
	char line[256];
	int rss = -1;
	if (!f) return -1;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "VmRSS: %d kB", &rss) == 1)
			break;
	fclose(f);
	return rss;
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

/* Run one iteration. On success returns 0 and fills *latency_ns and
 * *rss_after_kb (RSS sampled while the MR is registered). On failure
 * returns -1 and fills *errno_out. */
static int run_one(struct ibv_pd *pd, const struct case_def *c, size_t size,
		   uint64_t *latency_ns, int *rss_after_kb, int *errno_out)
{
	void *buf = NULL;
	uint64_t t0, t1;
	struct ibv_mr *mr;

	if (c->is_implicit) {
		t0 = ns_now();
		mr = ibv_reg_mr(pd, NULL, SIZE_MAX, c->access);
		t1 = ns_now();
		if (!mr) { *errno_out = errno; return -1; }
		*rss_after_kb = read_rss_kb();
		ibv_dereg_mr(mr);
		*latency_ns = t1 - t0;
		return 0;
	}

	buf = mmap(NULL, size, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) { *errno_out = errno; return -1; }
	/* Touch every page so the cost of fault-in does not bleed into the
	 * registration measurement. */
	memset(buf, 1, size);

	t0 = ns_now();
	mr = ibv_reg_mr(pd, buf, size, c->access);
	t1 = ns_now();
	if (!mr) {
		*errno_out = errno;
		munmap(buf, size);
		return -1;
	}
	*rss_after_kb = read_rss_kb();
	ibv_dereg_mr(mr);
	munmap(buf, size);
	*latency_ns = t1 - t0;
	return 0;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

int main(void)
{
	struct ibv_context *ctx = open_first();
	if (!ctx) { fprintf(stderr, "no device\n"); return 77; }
	struct ibv_pd *pd = ibv_alloc_pd(ctx);
	if (!pd) { perror("alloc_pd"); return 1; }

	int rss_baseline = read_rss_kb();

	/* Build a (case, size) plan with N_ITERS samples per bucket, then
	 * shuffle so buckets are interleaved across the run. */
	int n_plan = N_CASES * N_SIZES * N_ITERS;
	struct { int c, s; } *plan = malloc(n_plan * sizeof(*plan));
	if (!plan) { perror("malloc"); return 1; }
	int p = 0;
	for (int it = 0; it < N_ITERS; it++)
		for (int s = 0; s < N_SIZES; s++)
			for (int c = 0; c < N_CASES; c++) {
				plan[p].c = c; plan[p].s = s; p++;
			}
	srand((unsigned)time(NULL));
	for (int i = n_plan - 1; i > 0; i--) {
		int j = rand() % (i + 1);
		__typeof__(plan[0]) t = plan[i]; plan[i] = plan[j]; plan[j] = t;
	}

	/* Per-bucket accumulators. */
	uint64_t (*samples)[N_SIZES][N_ITERS] = calloc(N_CASES, sizeof(*samples));
	int (*nsamp)[N_SIZES] = calloc(N_CASES, sizeof(*nsamp));
	int (*nfail)[N_SIZES] = calloc(N_CASES, sizeof(*nfail));
	int (*last_err)[N_SIZES] = calloc(N_CASES, sizeof(*last_err));
	int (*rss_peak)[N_SIZES] = calloc(N_CASES, sizeof(*rss_peak));
	if (!samples || !nsamp || !nfail || !last_err || !rss_peak) {
		perror("calloc"); return 1;
	}

	/* One warmup pass per (case, size) bucket so the first measured iter
	 * does not pay first-touch cost on the kernel side. I just run one
	 * extra iter per bucket up front and discard the result. */
	for (int s = 0; s < N_SIZES; s++)
		for (int c = 0; c < N_CASES; c++) {
			uint64_t lat; int rss_kb, err;
			(void)run_one(pd, &cases[c], sizes[s], &lat, &rss_kb, &err);
		}

	/* Measurement pass. */
	for (int i = 0; i < n_plan; i++) {
		const struct case_def *cs = &cases[plan[i].c];
		size_t sz = sizes[plan[i].s];
		uint64_t lat = 0; int rss_kb = -1, err = 0;
		int rc = run_one(pd, cs, sz, &lat, &rss_kb, &err);
		if (rc == 0) {
			int n = nsamp[plan[i].c][plan[i].s];
			samples[plan[i].c][plan[i].s][n] = lat;
			nsamp[plan[i].c][plan[i].s] = n + 1;
			if (rss_kb > rss_peak[plan[i].c][plan[i].s])
				rss_peak[plan[i].c][plan[i].s] = rss_kb;
		} else {
			nfail[plan[i].c][plan[i].s]++;
			last_err[plan[i].c][plan[i].s] = err;
		}
	}

	/* Emit aggregated CSV. */
	printf("mode,requested_size,actual_addr_arg,actual_length_arg,"
	       "access_flags,median_ns,p95_ns,p99_ns,min_ns,max_ns,"
	       "fail_count,errno,rss_before_kb,rss_after_kb\n");

	for (int c = 0; c < N_CASES; c++) {
		const struct case_def *cs = &cases[c];
		for (int s = 0; s < N_SIZES; s++) {
			int n = nsamp[c][s];
			uint64_t med = 0, p95 = 0, p99 = 0, lo = 0, hi = 0;
			if (n > 0) {
				qsort(samples[c][s], n, sizeof(uint64_t), cmp_u64);
				lo  = samples[c][s][0];
				hi  = samples[c][s][n - 1];
				med = samples[c][s][n / 2];
				/* Standard rank: index = floor((n-1) * p). */
				p95 = samples[c][s][((n - 1) * 95) / 100];
				p99 = samples[c][s][((n - 1) * 99) / 100];
			}
			char addr_arg[16], len_arg[24];
			if (cs->is_implicit) {
				snprintf(addr_arg, sizeof(addr_arg), "NULL");
				snprintf(len_arg, sizeof(len_arg), "SIZE_MAX");
			} else {
				snprintf(addr_arg, sizeof(addr_arg), "non-null");
				snprintf(len_arg, sizeof(len_arg), "%zu", sizes[s]);
			}
			printf("%s,%zu,%s,%s,%s,"
			       "%lu,%lu,%lu,%lu,%lu,"
			       "%d,%d,%d,%d\n",
				cs->mode, sizes[s], addr_arg, len_arg,
				cs->access_flags_str,
				(unsigned long)med, (unsigned long)p95,
				(unsigned long)p99, (unsigned long)lo,
				(unsigned long)hi,
				nfail[c][s], last_err[c][s],
				rss_baseline, rss_peak[c][s]);
		}
	}

	free(plan); free(samples); free(nsamp); free(nfail);
	free(last_err); free(rss_peak);
	ibv_dealloc_pd(pd);
	ibv_close_device(ctx);
	return 0;
}
