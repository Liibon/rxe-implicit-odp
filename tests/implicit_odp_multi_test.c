/* Two RDMA WRITEs through one implicit ODP local lkey, from two source
 * buffers that I deliberately place in different 2 MiB chunks. This is
 * the test the single-slot child cache could not pass: each WR creates a
 * different child umem and both must stay live in the xarray.
 *
 * Source is an 8 MiB anonymous region; I use offset 0 and offset 4 MiB.
 * Destination is a 4 MiB region with an explicit MR.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <infiniband/verbs.h>

#define SRC_BYTES   (8 * 1024 * 1024)   /* spans multiple 2 MiB chunks */
#define DST_BYTES   (4 * 1024 * 1024)
#define XFER_BYTES  (1 * 1024 * 1024)
#define OFFSET_A    (0)
#define OFFSET_B    (4 * 1024 * 1024)   /* in a different 2 MiB chunk */
#define MAGIC_A     0x11
#define MAGIC_B     0x22

static struct ibv_context *open_first(void)
{
	int n = 0;
	struct ibv_device **list = ibv_get_device_list(&n);
	if (!list || n == 0) return NULL;
	struct ibv_context *ctx = ibv_open_device(list[0]);
	ibv_free_device_list(list);
	return ctx;
}

static int qp_to_rts(struct ibv_qp *qp, struct ibv_context *ctx,
		     uint8_t port, uint32_t dest_qpn,
		     uint16_t dlid, const uint8_t *dgid)
{
	struct ibv_qp_attr attr = {0};

	attr.qp_state        = IBV_QPS_INIT;
	attr.pkey_index      = 0;
	attr.port_num        = port;
	attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
	if (ibv_modify_qp(qp, &attr,
		IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
		return -1;

	attr = (struct ibv_qp_attr){0};
	attr.qp_state       = IBV_QPS_RTR;
	attr.path_mtu       = IBV_MTU_1024;
	attr.dest_qp_num    = dest_qpn;
	attr.rq_psn         = 0;
	attr.max_dest_rd_atomic = 1;
	attr.min_rnr_timer  = 12;
	attr.ah_attr.is_global    = 1;
	attr.ah_attr.dlid         = dlid;
	attr.ah_attr.sl           = 0;
	attr.ah_attr.src_path_bits = 0;
	attr.ah_attr.port_num      = port;
	memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
	attr.ah_attr.grh.hop_limit = 1;
	attr.ah_attr.grh.sgid_index = 0;
	if (ibv_modify_qp(qp, &attr,
		IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
		IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER))
		return -1;

	attr = (struct ibv_qp_attr){0};
	attr.qp_state      = IBV_QPS_RTS;
	attr.sq_psn        = 0;
	attr.timeout       = 14;
	attr.retry_cnt     = 7;
	attr.rnr_retry     = 7;
	attr.max_rd_atomic = 1;
	if (ibv_modify_qp(qp, &attr,
		IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
		IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC))
		return -1;
	return 0;
}

static int post_and_wait(struct ibv_qp *qp, struct ibv_cq *cq,
			 uint64_t src_va, uint32_t lkey,
			 uint64_t dst_va, uint32_t rkey,
			 int len, const char *tag)
{
	struct ibv_sge sge = {
		.addr = src_va,
		.length = len,
		.lkey = lkey,
	};
	struct ibv_send_wr wr = {
		.opcode = IBV_WR_RDMA_WRITE,
		.sg_list = &sge,
		.num_sge = 1,
		.send_flags = IBV_SEND_SIGNALED,
		.wr.rdma.remote_addr = dst_va,
		.wr.rdma.rkey = rkey,
	};
	struct ibv_send_wr *bad = NULL;
	struct ibv_wc wc;
	int n;

	if (ibv_post_send(qp, &wr, &bad)) {
		perror("post_send");
		return 1;
	}
	do { n = ibv_poll_cq(cq, 1, &wc); } while (n == 0);
	if (n < 0 || wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "[FAIL] %s: wc status=%d (%s)\n", tag,
			wc.status, ibv_wc_status_str(wc.status));
		return 1;
	}
	return 0;
}

int main(void)
{
	struct ibv_context *ctx = open_first();
	if (!ctx) { fprintf(stderr, "no device\n"); return 77; }

	struct ibv_port_attr port_attr;
	if (ibv_query_port(ctx, 1, &port_attr)) { perror("query_port"); return 1; }
	union ibv_gid gid;
	if (ibv_query_gid(ctx, 1, 0, &gid)) { perror("query_gid"); return 1; }

	struct ibv_pd *pd = ibv_alloc_pd(ctx);
	struct ibv_cq *scq = ibv_create_cq(ctx, 16, NULL, NULL, 0);
	struct ibv_cq *rcq = ibv_create_cq(ctx, 16, NULL, NULL, 0);

	void *dst = mmap(NULL, DST_BYTES, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	memset(dst, 0, DST_BYTES);
	struct ibv_mr *dst_mr = ibv_reg_mr(pd, dst, DST_BYTES,
		IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
	if (!dst_mr) { perror("reg_mr dst"); return 1; }

	/* One implicit ODP MR. The two source offsets live in different
	 * 2 MiB chunks, so an honest implementation must create and hold two
	 * child umems concurrently. */
	struct ibv_mr *src_mr = ibv_reg_mr(pd, NULL, SIZE_MAX,
		IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE);
	if (!src_mr) { perror("reg_mr implicit"); return 1; }

	void *src = mmap(NULL, SRC_BYTES, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	memset((char *)src + OFFSET_A, MAGIC_A, XFER_BYTES);
	memset((char *)src + OFFSET_B, MAGIC_B, XFER_BYTES);

	struct ibv_qp_init_attr qia = {
		.send_cq = scq, .recv_cq = rcq,
		.cap = { .max_send_wr = 4, .max_recv_wr = 4,
			 .max_send_sge = 1, .max_recv_sge = 1 },
		.qp_type = IBV_QPT_RC,
	};
	struct ibv_qp *qp_c = ibv_create_qp(pd, &qia);
	struct ibv_qp *qp_s = ibv_create_qp(pd, &qia);
	if (!qp_c || !qp_s) { perror("create_qp"); return 1; }

	if (qp_to_rts(qp_c, ctx, 1, qp_s->qp_num, port_attr.lid, gid.raw)) return 1;
	if (qp_to_rts(qp_s, ctx, 1, qp_c->qp_num, port_attr.lid, gid.raw)) return 1;

	if (post_and_wait(qp_c, scq,
			  (uintptr_t)src + OFFSET_A, src_mr->lkey,
			  (uintptr_t)dst + 0, dst_mr->rkey,
			  XFER_BYTES, "chunk A WRITE"))
		return 1;

	if (post_and_wait(qp_c, scq,
			  (uintptr_t)src + OFFSET_B, src_mr->lkey,
			  (uintptr_t)dst + XFER_BYTES, dst_mr->rkey,
			  XFER_BYTES, "chunk B WRITE"))
		return 1;

	int bad_a = 0, bad_b = 0;
	for (int i = 0; i < XFER_BYTES; i++) {
		if (((unsigned char *)dst)[i] != MAGIC_A) bad_a++;
		if (((unsigned char *)dst)[XFER_BYTES + i] != MAGIC_B) bad_b++;
	}
	if (bad_a || bad_b) {
		fprintf(stderr, "[FAIL] bad bytes: A=%d B=%d\n", bad_a, bad_b);
		return 1;
	}
	printf("[ OK ] two implicit-MR WRITEs from disjoint 2 MiB chunks: %d + %d bytes\n",
	       XFER_BYTES, XFER_BYTES);
	return 0;
}
