/* Shared helpers for the implicit ODP test programs.
 *
 * Device selection: --dev NAME or RXE_DEV=NAME. Default is the first
 * device returned by ibv_get_device_list, with a stderr warning.
 *
 * Deadline-bounded CQ poll: replaces the spin-forever loops that hung
 * the original write test when an SGE could not resolve. */

#ifndef ODP_HELPERS_H
#define ODP_HELPERS_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <infiniband/verbs.h>

static inline const char *odp_dev_from_args(int argc, char **argv)
{
	for (int i = 1; i + 1 < argc; i++)
		if (!strcmp(argv[i], "--dev"))
			return argv[i + 1];
	return getenv("RXE_DEV");
}

static inline struct ibv_context *
odp_open_device(int argc, char **argv)
{
	const char *want = odp_dev_from_args(argc, argv);
	int n = 0;
	struct ibv_device **list = ibv_get_device_list(&n);
	if (!list || n == 0) {
		fprintf(stderr, "no RDMA devices found\n");
		return NULL;
	}
	struct ibv_device *pick = NULL;
	if (want) {
		for (int i = 0; i < n; i++)
			if (!strcmp(ibv_get_device_name(list[i]), want)) {
				pick = list[i];
				break;
			}
		if (!pick) {
			fprintf(stderr, "device %s not found; available:", want);
			for (int i = 0; i < n; i++)
				fprintf(stderr, " %s", ibv_get_device_name(list[i]));
			fprintf(stderr, "\n");
			ibv_free_device_list(list);
			return NULL;
		}
	} else {
		pick = list[0];
		fprintf(stderr, "no --dev / RXE_DEV given, using %s (first device)\n",
			ibv_get_device_name(pick));
	}
	struct ibv_context *ctx = ibv_open_device(pick);
	if (!ctx)
		fprintf(stderr, "ibv_open_device(%s) failed\n",
			ibv_get_device_name(pick));
	ibv_free_device_list(list);
	return ctx;
}

static inline uint64_t odp_ms_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* Poll cq until one completion is delivered or deadline elapses.
 * Returns 1 on success (wc filled), 0 on timeout, -1 on poll error.
 * Caller checks wc.status separately on success. */
static inline int odp_poll_cq_deadline(struct ibv_cq *cq, struct ibv_wc *wc,
				       int timeout_ms, const char *stage)
{
	uint64_t deadline = odp_ms_now() + (uint64_t)timeout_ms;
	for (;;) {
		int n = ibv_poll_cq(cq, 1, wc);
		if (n > 0)
			return 1;
		if (n < 0)
			return -1;
		if (odp_ms_now() >= deadline) {
			fprintf(stderr,
				"[TIMEOUT] cq poll exceeded %d ms during stage: %s\n",
				timeout_ms, stage);
			return 0;
		}
	}
}

#endif /* ODP_HELPERS_H */
