/* Local SGE write using an implicit ODP lkey on a loopback RXE pair.
 * I keep both QPs on the same device so the test runs in a single VM
 * without needing two hosts. The goal is to prove the lkey actually works
 * in a real verbs operation, not just that registration succeeded. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <infiniband/verbs.h>

#define BUF_BYTES (64 * 1024)
#define MAGIC     0xA5

static struct ibv_context *open_first(void)
{
	int n = 0;
	struct ibv_device **list = ibv_get_device_list(&n);
	if (!list || n == 0) return NULL;
	struct ibv_context *ctx = ibv_open_device(list[0]);
	ibv_free_device_list(list);
	return ctx;
}

struct conn {
	struct ibv_qp *qp;
	uint32_t qpn;
	uint16_t lid;
	uint8_t  gid[16];
};

/* Bring a QP up to RTS for RC. I share the same path for both sides since
 * this is loopback. */
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
		IBV_QP_RNR_RETRY | IBV_QP_MAX_RD_ATOMIC))
		return -1;
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

	/* Server side: explicit MR over a fixed buffer. I want a known rkey
	 * to target with the RDMA WRITE coming from the client. */
	void *dst = mmap(NULL, BUF_BYTES, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	memset(dst, 0, BUF_BYTES);
	struct ibv_mr *dst_mr = ibv_reg_mr(pd, dst, BUF_BYTES,
		IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
	if (!dst_mr) { perror("reg_mr dst"); return 1; }

	/* Client side: implicit ODP local lkey. The source buffer is plain
	 * mmap memory the kernel must fault in on first SGE use. */
	struct ibv_mr *src_mr = ibv_reg_mr(pd, NULL, SIZE_MAX,
		IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE);
	if (!src_mr) { perror("reg_mr implicit"); return 1; }

	void *src = mmap(NULL, BUF_BYTES, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	memset(src, MAGIC, BUF_BYTES);

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

	struct ibv_sge sge = {
		.addr = (uintptr_t)src,
		.length = BUF_BYTES,
		.lkey = src_mr->lkey,
	};
	struct ibv_send_wr wr = {
		.opcode = IBV_WR_RDMA_WRITE,
		.sg_list = &sge,
		.num_sge = 1,
		.send_flags = IBV_SEND_SIGNALED,
		.wr.rdma.remote_addr = (uintptr_t)dst,
		.wr.rdma.rkey = dst_mr->rkey,
	};
	struct ibv_send_wr *bad = NULL;
	if (ibv_post_send(qp_c, &wr, &bad)) { perror("post_send"); return 1; }

	struct ibv_wc wc;
	int n;
	do { n = ibv_poll_cq(scq, 1, &wc); } while (n == 0);
	if (n < 0 || wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "WC status=%d (%s)\n", wc.status,
			ibv_wc_status_str(wc.status));
		return 1;
	}

	int bad_bytes = 0;
	for (int i = 0; i < BUF_BYTES; i++)
		if (((unsigned char *)dst)[i] != MAGIC) bad_bytes++;

	if (bad_bytes) {
		fprintf(stderr, "[FAIL] %d/%d bytes wrong\n", bad_bytes, BUF_BYTES);
		return 1;
	}
	printf("[ OK ] implicit lkey RDMA WRITE delivered %d bytes\n", BUF_BYTES);
	return 0;
}
