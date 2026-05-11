/* Registration accept/reject behavior for local-access implicit ODP on RXE.
 * I run each case with a fresh PD so a prior failure cannot pollute state. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <infiniband/verbs.h>

static struct ibv_context *open_first(void)
{
	int n = 0;
	struct ibv_device **list = ibv_get_device_list(&n);
	if (!list || n == 0) {
		fprintf(stderr, "no RDMA devices\n");
		return NULL;
	}
	/* I pick the first device. In my VM that is the rxe device I add by name. */
	struct ibv_context *ctx = ibv_open_device(list[0]);
	ibv_free_device_list(list);
	return ctx;
}

struct case_t {
	const char *name;
	int access;
	int expect_ok;
};

static int run_case(struct ibv_context *ctx, const struct case_t *c)
{
	struct ibv_pd *pd = ibv_alloc_pd(ctx);
	if (!pd) { perror("ibv_alloc_pd"); return 1; }

	errno = 0;
	struct ibv_mr *mr = ibv_reg_mr(pd, NULL, SIZE_MAX, c->access);
	int ok = (mr != NULL);
	int saved = errno;

	if (ok != c->expect_ok) {
		fprintf(stderr, "[FAIL] %s: expected %s, got %s (errno=%d)\n",
			c->name,
			c->expect_ok ? "ACCEPT" : "REJECT",
			ok ? "ACCEPT" : "REJECT",
			saved);
		if (mr) ibv_dereg_mr(mr);
		ibv_dealloc_pd(pd);
		return 1;
	}
	fprintf(stdout, "[ OK ] %s\n", c->name);
	if (mr) ibv_dereg_mr(mr);
	ibv_dealloc_pd(pd);
	return 0;
}

int main(void)
{
	struct ibv_context *ctx = open_first();
	if (!ctx) return 77; /* skip code */

	/* Cases ordered from cheapest to most pathological. */
	/* "no access" is valid in the ABI as a read-only source lkey, so the
	 * prototype accepts it. The interesting cases are local-write
	 * (accept) and any remote bit (reject). */
	const struct case_t cases[] = {
		{ "implicit + local_write",   IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE,   1 },
		{ "implicit + no access",     IBV_ACCESS_ON_DEMAND,                            1 },
		{ "implicit + remote_write",  IBV_ACCESS_ON_DEMAND | IBV_ACCESS_REMOTE_WRITE,  0 },
		{ "implicit + remote_read",   IBV_ACCESS_ON_DEMAND | IBV_ACCESS_REMOTE_READ,   0 },
		{ "implicit + remote_atomic", IBV_ACCESS_ON_DEMAND | IBV_ACCESS_REMOTE_ATOMIC, 0 },
	};

	int fails = 0;
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
		fails += run_case(ctx, &cases[i]);

	ibv_close_device(ctx);
	return fails ? 1 : 0;
}
