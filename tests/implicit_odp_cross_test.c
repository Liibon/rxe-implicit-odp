/* One RDMA WRITE whose single SGE straddles a 2 MiB chunk boundary on an
 * implicit ODP MR. This is the sharp test for the chunk loop: a naive
 * implementation that resolves only the first child would lose the
 * second half of the transfer.
 *
 * Layout. A 4 MiB anonymous mapping covers two 2 MiB chunks (relative to
 * the chunk grid that the kernel uses, RXE_ODP_CHILD_SIZE). I pick the
 * SGE so it starts 64 KiB before a 2 MiB boundary and ends 64 KiB after.
 * 128 KiB total, deliberately spanning two children. To make the
 * boundary deterministic I align the buffer to 2 MiB first via
 * posix_memalign.
 *
 * Device selection: --dev / RXE_DEV. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "helpers.h"

#define CHUNK_SIZE       (2UL * 1024 * 1024)
#define SRC_BYTES        (4 * 1024 * 1024)
#define DST_BYTES        (256 * 1024)
#define XFER_BYTES       (128 * 1024)
#define LEAD_BYTES       (64 * 1024)   /* bytes before the boundary */
#define MAGIC            0x5A
#define POLL_TIMEOUT_MS  5000

static int qp_to_rts(struct ibv_qp *qp, uint8_t port, uint32_t dest_qpn,
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

int main(int argc, char **argv)
{
	struct ibv_context *ctx = odp_open_device(argc, argv);
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

	struct ibv_mr *src_mr = ibv_reg_mr(pd, NULL, SIZE_MAX,
		IBV_ACCESS_ON_DEMAND | IBV_ACCESS_LOCAL_WRITE);
	if (!src_mr) { perror("reg_mr implicit"); return 1; }

	/* 2 MiB-aligned 4 MiB buffer. The buffer is split into two halves by
	 * the chunk boundary at offset 2 MiB. The SGE starts 64 KiB into the
	 * first half and runs 128 KiB total, so it crosses into the second
	 * half. */
	void *src_raw = NULL;
	if (posix_memalign(&src_raw, CHUNK_SIZE, SRC_BYTES) != 0) {
		perror("posix_memalign");
		return 1;
	}
	memset(src_raw, MAGIC, SRC_BYTES);
	uintptr_t src_base = (uintptr_t)src_raw;
	uintptr_t sge_start = src_base + (CHUNK_SIZE - LEAD_BYTES);
	uintptr_t sge_end   = sge_start + XFER_BYTES;
	if ((sge_start >> 21) == (sge_end >> 21)) {
		fprintf(stderr, "[FAIL] SGE does not span chunks: 0x%lx..0x%lx\n",
			(unsigned long)sge_start, (unsigned long)sge_end);
		return 1;
	}
	printf("SGE [0x%lx..0x%lx) crosses chunk boundary at 0x%lx\n",
	       (unsigned long)sge_start, (unsigned long)sge_end,
	       (unsigned long)(src_base + CHUNK_SIZE));

	struct ibv_qp_init_attr qia = {
		.send_cq = scq, .recv_cq = rcq,
		.cap = { .max_send_wr = 4, .max_recv_wr = 4,
			 .max_send_sge = 1, .max_recv_sge = 1 },
		.qp_type = IBV_QPT_RC,
	};
	struct ibv_qp *qp_c = ibv_create_qp(pd, &qia);
	struct ibv_qp *qp_s = ibv_create_qp(pd, &qia);
	if (!qp_c || !qp_s) { perror("create_qp"); return 1; }

	if (qp_to_rts(qp_c, 1, qp_s->qp_num, port_attr.lid, gid.raw)) return 1;
	if (qp_to_rts(qp_s, 1, qp_c->qp_num, port_attr.lid, gid.raw)) return 1;

	struct ibv_sge sge = {
		.addr = sge_start,
		.length = XFER_BYTES,
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
	int r = odp_poll_cq_deadline(scq, &wc, POLL_TIMEOUT_MS,
				     "cross-chunk RDMA WRITE");
	if (r == 0) return 1;
	if (r < 0) { perror("poll_cq"); return 1; }
	if (wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "WC status=%d (%s)\n", wc.status,
			ibv_wc_status_str(wc.status));
		return 1;
	}

	int bad_bytes = 0;
	for (int i = 0; i < XFER_BYTES; i++)
		if (((unsigned char *)dst)[i] != MAGIC) bad_bytes++;
	if (bad_bytes) {
		fprintf(stderr, "[FAIL] %d/%d bytes wrong\n", bad_bytes, XFER_BYTES);
		return 1;
	}
	printf("[ OK ] single SGE crossing 2 MiB chunk boundary delivered %d bytes\n",
	       XFER_BYTES);
	free(src_raw);
	return 0;
}
