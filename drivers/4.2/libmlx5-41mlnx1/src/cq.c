/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <infiniband/opcode.h>

#include <assert.h>
#include "mlx5.h"
#include "wqe.h"
#include "doorbell.h"

enum {
	CQ_OK					=  0,
	CQ_EMPTY				= -1,
	CQ_POLL_ERR				= -2
};

#define MLX5_CQ_DB_REQ_NOT_SOL			(1 << 24)
#define MLX5_CQ_DB_REQ_NOT			(0 << 24)

enum {
	MLX5_CQ_MODIFY_RESEIZE = 0,
	MLX5_CQ_MODIFY_MODER = 1,
	MLX5_CQ_MODIFY_MAPPING = 2,
};

enum {
	MLX5_NO_INLINE_DATA	= 0x0,
	MLX5_INLINE_DATA32_SEG	= 0x1,
	MLX5_INLINE_DATA64_SEG	= 0x2,
	MLX5_COMPRESSED		= 0x3,
};

enum {
	MLX5_CQE_L4_HDR_TYPE_TCP		= 0x10,
	MLX5_CQE_L4_HDR_TYPE_UDP		= 0x20,
	MLX5_CQE_L4_HDR_TYPE_TCP_EMP_ACK	= 0x30,
	MLX5_CQE_L4_HDR_TYPE_TCP_ACK		= 0x40,
};

enum {
	MLX5_CQE_L3_HDR_TYPE_MASK	= 0xC,
	MLX5_CQE_L4_HDR_TYPE_MASK	= 0x70,
};

enum {
	/* Masks to handle the CQE byte_count field in case of MP RQ */
	MP_RQ_BYTE_CNT_FIELD_MASK	= 0x0000FFFF,
	MP_RQ_NUM_STRIDES_FIELD_MASK	= 0x7FFF0000,
	MP_RQ_FILLER_FIELD_MASK		= 0x80000000,
	MP_RQ_NUM_STRIDES_FIELD_SHIFT	= 16,
};

enum {
	MLX5_TM_MAX_SYNC_DIFF = 0x3fff
};

int mlx5_stall_num_loop = 60;
int mlx5_stall_cq_poll_min = 60;
int mlx5_stall_cq_poll_max = 100000;
int mlx5_stall_cq_inc_step = 100;
int mlx5_stall_cq_dec_step = 10;

#define MLX5E_CQE_FORMAT_MASK 0xc
static inline int mlx5_get_cqe_format(struct mlx5_cqe64 *cqe)
{
	return (cqe->op_own & MLX5E_CQE_FORMAT_MASK) >> 2;
}

#define LAST_PEEK_ENTRY (-1U)
#define PEEK_ENTRY(cq, n) \
	(n == LAST_PEEK_ENTRY ? NULL : \
	((struct mlx5_peek_entry *)cq->peer_buf.buf) + n)
#define PEEK_ENTRY_N(cq, pe) \
	(pe == NULL ? LAST_PEEK_ENTRY : \
	((pe - (struct mlx5_peek_entry *)cq->peer_buf.buf)))

static void *get_buf_cqe(struct mlx5_buf *buf, int n, int cqe_sz)
{
	return buf->buf + n * cqe_sz;
}

static void *get_cqe(struct mlx5_cq *cq, int n)
{
	return cq->active_buf->buf + n * cq->cqe_sz;
}

static inline void *get_sw_cqe(struct mlx5_cq *cq, int n, int cqe_mask) __attribute__((always_inline));
static inline void *get_sw_cqe(struct mlx5_cq *cq, int n, int cqe_mask)
{
	void *cqe = get_cqe(cq, n & cqe_mask);
	struct mlx5_cqe64 *cqe64;

	cqe64 = (cq->cqe_sz == 64) ? cqe : cqe + 64;

	if (likely((cqe64->op_own) >> 4 != MLX5_CQE_INVALID) &&
	    !((cqe64->op_own & MLX5_CQE_OWNER_MASK) ^ !!(n & (cqe_mask + 1)))) {
		return cqe;
	} else {
		return NULL;
	}
}

static inline struct mlx5_cqe64 *get_next_cqe(struct mlx5_cq *cq, const int cqe_sz)
{
	unsigned idx = cq->cons_index & cq->ibv_cq.cqe;
	void *cqe = cq->active_buf->buf + idx * 64;
	struct mlx5_cqe64 *cqe64;

	if (unlikely(0))
		return &cq->next_decomp_cqe64;

	if (unlikely(0)) {
		struct mlx5_peek_entry *tmp;

		while (cq->peer_peek_table[idx]) {
			if (cq->peer_peek_table[idx]->busy) {
				errno = EBUSY;
				return NULL;
			}
			tmp = cq->peer_peek_table[idx];
			cq->peer_peek_table[idx] = PEEK_ENTRY(cq, tmp->next);
			tmp->next = PEEK_ENTRY_N(cq, cq->peer_peek_free);
			cq->peer_peek_free = tmp;
		}
	}

	cqe64 = cqe;

	if (likely((cqe64->op_own) >> 4 != MLX5_CQE_INVALID) &&
	    !((cqe64->op_own & MLX5_CQE_OWNER_MASK) ^ !!(cq->cons_index & (cq->ibv_cq.cqe + 1)))) {
		return cqe64;
	}
	return NULL;
}

static inline void handle_good_req(struct ibv_wc *wc, struct mlx5_cqe64 *cqe, struct mlx5_wq *wq, int idx)
{
	switch (ntohl(cqe->sop_drop_qpn) >> 24) {
	case MLX5_OPCODE_RDMA_WRITE_IMM:
		wc->wc_flags |= IBV_WC_WITH_IMM;
	case MLX5_OPCODE_RDMA_WRITE:
		wc->opcode    = IBV_WC_RDMA_WRITE;
		break;
	case MLX5_OPCODE_SEND_IMM:
		wc->wc_flags |= IBV_WC_WITH_IMM;
	case MLX5_OPCODE_SEND:
	case MLX5_OPCODE_SEND_INVAL:
		wc->opcode    = IBV_WC_SEND;
		break;
	case MLX5_OPCODE_RDMA_READ:
		wc->opcode    = IBV_WC_RDMA_READ;
		wc->byte_len  = ntohl(cqe->byte_cnt);
		break;
	case MLX5_OPCODE_ATOMIC_CS:
		wc->opcode    = IBV_WC_COMP_SWAP;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_ATOMIC_FA:
		wc->opcode    = IBV_WC_FETCH_ADD;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_UMR:
		wc->opcode    = wq->wr_data[idx];
		break;

	case MLX5_OPCODE_ATOMIC_MASKED_CS:
		wc->opcode    = IBV_EXP_WC_MASKED_COMP_SWAP;
		wc->byte_len  = ntohl(cqe->byte_cnt);
		break;

	case MLX5_OPCODE_ATOMIC_MASKED_FA:
		wc->opcode    = IBV_EXP_WC_MASKED_FETCH_ADD;
		wc->byte_len  = ntohl(cqe->byte_cnt);
		break;
	case MLX5_OPCODE_TSO:
		wc->opcode    = IBV_EXP_WC_TSO;
		break;
	}
}

static inline int handle_responder(struct ibv_wc *wc, struct mlx5_cqe64 *cqe,
			    struct mlx5_qp *qp, struct mlx5_srq *srq,
			    enum mlx5_rsc_type type,
			    uint64_t *exp_wc_flags)
{
  //assert(srq == NULL);  Commented for perf, but true
	uint16_t	wqe_ctr;
	struct mlx5_wq *wq;
	int err = 0;

	wc->byte_len = ntohl(cqe->byte_cnt);
	if (0) {
		wqe_ctr = ntohs(cqe->wqe_counter);
		wc->wr_id = srq->wrid[wqe_ctr];
		mlx5_free_srq_wqe(srq, wqe_ctr);
		if (0)
			err = mlx5_copy_to_recv_srq(srq, wqe_ctr, cqe,
						    wc->byte_len);
		else if (0)
			err = mlx5_copy_to_recv_srq(srq, wqe_ctr, cqe - 1,
						    wc->byte_len);
	} else {
		wq	  = &qp->rq;
		wqe_ctr = wq->tail & (wq->wqe_cnt - 1);
    __builtin_prefetch((void *)(wq->wrid[wqe_ctr]), 0, 3);
		++wq->tail;
		if (0)
			err = mlx5_copy_to_recv_wqe(qp, wqe_ctr, cqe,
						    wc->byte_len);
		else if (0)
			err = mlx5_copy_to_recv_wqe(qp, wqe_ctr, cqe - 1,
						    wc->byte_len);
	}
	if (err)
		return err;

#if 0 // Unneeded fields of wc
	wc->byte_len = ntohl(cqe->byte_cnt);

	switch (cqe->op_own >> 4) {
	case MLX5_CQE_RESP_WR_IMM:
		wc->opcode	= IBV_WC_RECV_RDMA_WITH_IMM;
		wc->wc_flags	|= IBV_WC_WITH_IMM;
		wc->imm_data = cqe->imm_inval_pkey;
		break;
	case MLX5_CQE_RESP_SEND:
		wc->opcode   = IBV_WC_RECV;
		break;
	case MLX5_CQE_RESP_SEND_IMM:
		wc->opcode	= IBV_WC_RECV;
		wc->wc_flags	|= IBV_WC_WITH_IMM;
		wc->imm_data = cqe->imm_inval_pkey;
		break;
	case MLX5_CQE_RESP_SEND_INV:
		wc->opcode = IBV_WC_RECV;
		wc->wc_flags	|= IBV_WC_WITH_INV;
		wc->imm_data = ntohl(cqe->imm_inval_pkey);
		break;
	}
	wc->slid	   = ntohs(cqe->slid);
	wc->sl		   = (ntohl(cqe->flags_rqpn) >> 24) & 0xf;
	if (srq && (type != MLX5_RSC_TYPE_DCT) &&
	    ((type == MLX5_RSC_TYPE_INVAL) || (type == MLX5_RSC_TYPE_XSRQ) ||
	     ((qp->verbs_qp.qp.qp_type == IBV_QPT_XRC_RECV) ||
	      (qp->verbs_qp.qp.qp_type == IBV_QPT_XRC))))
		wc->src_qp	   = srq->srqn;
	else
		wc->src_qp	   = ntohl(cqe->flags_rqpn) & 0xffffff;


	wc->dlid_path_bits = cqe->ml_path & 0x7f;

	if ((qp && qp->verbs_qp.qp.qp_type == IBV_QPT_UD) ||
	    (type == MLX5_RSC_TYPE_DCT)) {
		g = (ntohl(cqe->flags_rqpn) >> 28) & 3;
		wc->wc_flags |= g ? IBV_WC_GRH : 0;
	}

	wc->pkey_index     = ntohl(cqe->imm_inval_pkey) & 0xffff;
#endif

	return IBV_WC_SUCCESS;
}

static void dump_cqe(FILE *fp, void *buf)
{
	uint32_t *p = buf;
	int i;

	for (i = 0; i < 16; i += 4)
		fprintf(fp, "%08x %08x %08x %08x\n", ntohl(p[i]), ntohl(p[i + 1]),
			ntohl(p[i + 2]), ntohl(p[i + 3]));
}

static void mlx5_set_bad_wc_opcode(struct ibv_exp_wc *wc,
				   struct mlx5_err_cqe *cqe,
				   uint8_t is_req,
				   uint8_t *is_umr)
{
	if (is_req) {
		switch (ntohl(cqe->s_wqe_opcode_qpn) >> 24) {
		case MLX5_OPCODE_RDMA_WRITE_IMM:
		case MLX5_OPCODE_RDMA_WRITE:
			wc->exp_opcode    = IBV_EXP_WC_RDMA_WRITE;
			break;
		case MLX5_OPCODE_SEND_IMM:
		case MLX5_OPCODE_SEND:
		case MLX5_OPCODE_SEND_INVAL:
			wc->exp_opcode    = IBV_EXP_WC_SEND;
			break;
		case MLX5_OPCODE_RDMA_READ:
			wc->exp_opcode    = IBV_EXP_WC_RDMA_READ;
			break;
		case MLX5_OPCODE_ATOMIC_CS:
			wc->exp_opcode    = IBV_EXP_WC_COMP_SWAP;
			break;
		case MLX5_OPCODE_ATOMIC_FA:
			wc->exp_opcode    = IBV_EXP_WC_FETCH_ADD;
			break;
		case MLX5_OPCODE_UMR:
			*is_umr = 1;
			break;
		case MLX5_OPCODE_ATOMIC_MASKED_CS:
			wc->exp_opcode    = IBV_EXP_WC_MASKED_COMP_SWAP;
			break;
		case MLX5_OPCODE_ATOMIC_MASKED_FA:
			wc->exp_opcode    = IBV_EXP_WC_MASKED_FETCH_ADD;
			break;
		case MLX5_OPCODE_TSO:
			wc->exp_opcode    = IBV_EXP_WC_TSO;
			break;
		}
	} else {
		switch (cqe->op_own >> 4) {
		case MLX5_CQE_RESP_WR_IMM:
			wc->exp_opcode	= IBV_EXP_WC_RECV_RDMA_WITH_IMM;
			break;
		case MLX5_CQE_RESP_SEND:
			wc->exp_opcode   = IBV_EXP_WC_RECV;
			break;
		case MLX5_CQE_RESP_SEND_IMM:
			wc->exp_opcode	= IBV_EXP_WC_RECV;
			break;
		}
	}
}

static void mlx5_handle_error_cqe(struct mlx5_err_cqe *cqe,
				  struct ibv_exp_wc *wc)
{
	switch (cqe->syndrome) {
	case MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IBV_WC_LOC_LEN_ERR;
		break;
	case MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IBV_WC_LOC_QP_OP_ERR;
		break;
	case MLX5_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IBV_WC_LOC_PROT_ERR;
		break;
	case MLX5_CQE_SYNDROME_WR_FLUSH_ERR:
		wc->status = IBV_WC_WR_FLUSH_ERR;
		break;
	case MLX5_CQE_SYNDROME_MW_BIND_ERR:
		wc->status = IBV_WC_MW_BIND_ERR;
		break;
	case MLX5_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IBV_WC_BAD_RESP_ERR;
		break;
	case MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IBV_WC_LOC_ACCESS_ERR;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IBV_WC_REM_INV_REQ_ERR;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IBV_WC_REM_ACCESS_ERR;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IBV_WC_REM_OP_ERR;
		break;
	case MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IBV_WC_RETRY_EXC_ERR;
		break;
	case MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IBV_WC_RNR_RETRY_EXC_ERR;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		wc->status = IBV_WC_REM_ABORT_ERR;
		break;
	default:
		wc->status = IBV_WC_GENERAL_ERR;
		break;
	}

	wc->vendor_err = cqe->vendor_err_synd;
}

#if defined(__x86_64__) || defined (__i386__)
static inline unsigned long get_cycles()
{
	uint32_t low, high;
	uint64_t val;
	asm volatile ("rdtsc" : "=a" (low), "=d" (high));
	val = high;
	val = (val << 32) | low;
	return val;
}

static void mlx5_stall_poll_cq()
{
	int i;

	for (i = 0; i < mlx5_stall_num_loop; i++)
		(void)get_cycles();
}
static void mlx5_stall_cycles_poll_cq(uint64_t cycles)
{
	while (get_cycles()  <  cycles)
		; /* Nothing */
}
static void mlx5_get_cycles(uint64_t *cycles)
{
	*cycles = get_cycles();
}
#else
static void mlx5_stall_poll_cq()
{
}
static void mlx5_stall_cycles_poll_cq(uint64_t cycles)
{
}
static void mlx5_get_cycles(uint64_t *cycles)
{
}
#endif

static int is_requestor(uint8_t opcode)
{
	if (opcode == MLX5_CQE_REQ || opcode == MLX5_CQE_REQ_ERR)
		return 1;
	else
		return 0;
}

static int is_responder(uint8_t opcode)
{
	switch (opcode) {
	case MLX5_CQE_RESP_WR_IMM:
	case MLX5_CQE_RESP_SEND:
	case MLX5_CQE_RESP_SEND_IMM:
	case MLX5_CQE_RESP_SEND_INV:
	case MLX5_CQE_RESP_ERR:
	case MLX5_CQE_NO_PACKET:
		return 1;
	}

	return 0;
}

static inline void copy_cqes(struct mlx5_cq *cq, struct mlx5_mini_cqe8 *mini_array,
			     struct mlx5_cqe64 *title, int cnt, uint16_t *wqe_cnt, int cqe_idx,
			     const int mp_rq, int cqe_mask)
	__attribute__((always_inline));
static inline void copy_cqes(struct mlx5_cq *cq, struct mlx5_mini_cqe8 *mini_array,
			     struct mlx5_cqe64 *title, int cnt, uint16_t *wqe_cnt, int cqe_idx,
			     const int mp_rq, int cqe_mask)
{
	struct mlx5_cqe64 *cqe;
	int i;
	int is_req = is_requestor(title->op_own >> 4);
	int log_size = cq->cq_log_size;
	uint8_t opown = title->op_own & 0xf2;

	for (i = 0; i < cnt; i++) {
		cqe = get_cqe(cq, (cqe_idx + i) & cqe_mask);
		memcpy(cqe, title, sizeof(*title));
		cqe->byte_cnt = mini_array[i].byte_cnt;
		cqe->op_own = opown | (((cqe_idx + i) >> log_size) & 1);
		if (is_req) {
			cqe->wqe_counter = mini_array[i].s_wqe_info.wqe_counter;
			cqe->sop_qpn.sop = mini_array[i].s_wqe_info.s_wqe_opcode;
		} else {
			/* for now we are supporting only rx_hash_res not
			 * checksum */
			cqe->rx_hash_res = mini_array[i].rx_hash_result;
			cqe->wqe_counter = htons(*wqe_cnt);
			if (mp_rq)
				/*
				 * In case of mp_rq the wqe_cnt is the stride index of the message start,
				 * therefore we need to increase it by the number of consumed strides
				 */
				(*wqe_cnt) += (ntohl(mini_array[i].byte_cnt) & MP_RQ_NUM_STRIDES_FIELD_MASK) >>
					      MP_RQ_NUM_STRIDES_FIELD_SHIFT;
			else
				/*
				 * In case of non mp_rq the wqe_cnt is the sq/rq wqe counter,
				 * therefore we need to increase it by one
				 */
				(*wqe_cnt)++;
		}
	}
}

static inline struct mlx5_resource *find_rsc(struct mlx5_cq *cq,
					     struct mlx5_cqe64 *cqe64,
					     const int cqe_ver) __attribute__((always_inline));
static inline struct mlx5_resource *find_rsc(struct mlx5_cq *cq,
					     struct mlx5_cqe64 *cqe64,
					     const int cqe_ver)
{
	uint32_t srqn_uidx = ntohl(cqe64->srqn_uidx) & 0xffffff;
	uint32_t rsn;

	if (cqe_ver)
		return mlx5_find_uidx(to_mctx(cq->ibv_cq.context), srqn_uidx);

	rsn = ntohl(cqe64->sop_drop_qpn) & 0xffffff;

	return mlx5_find_rsc(to_mctx(cq->ibv_cq.context), rsn);
}

static inline void mlx5_decompress_cqe_idx(struct mlx5_cq *cq, uint32_t cqe_idx, int cqe_mask)
	__attribute__((always_inline));
static inline void mlx5_decompress_cqe_idx(struct mlx5_cq *cq, uint32_t cqe_idx, int cqe_mask)
{
	struct mlx5_cqe64 *title, *cqe;
	struct mlx5_mini_cqe8 mini_array[8];
	int cqe_cnt;
	uint16_t wqe_cnt;
	struct mlx5_resource *cur_rsc;
	int mp_rq;

	cqe = get_cqe(cq, cqe_idx & cqe_mask);
	title = cqe;
	memcpy(mini_array, get_cqe(cq, (cqe_idx + 1) & cqe_mask), sizeof(*title));
	cqe_cnt = ntohl(title->byte_cnt);
	wqe_cnt = ntohs(title->wqe_counter);
	cur_rsc = find_rsc(cq, title, (to_mctx(cq->ibv_cq.context))->cqe_version);
	mp_rq = cur_rsc ? cur_rsc->type == MLX5_RSC_TYPE_MP_RWQ : 0;

	for (; cqe_cnt > MLX5_MINI_ARR_SIZE - 1;
	     cqe_idx += MLX5_MINI_ARR_SIZE, cqe_cnt -= MLX5_MINI_ARR_SIZE) {
		copy_cqes(cq, mini_array, title, MLX5_MINI_ARR_SIZE, &wqe_cnt,
			  cqe_idx, mp_rq, cqe_mask);
		cqe = get_cqe(cq, (cqe_idx + MLX5_MINI_ARR_SIZE) &
				  cqe_mask);
		memcpy(mini_array, cqe, sizeof(*title));
	}

	copy_cqes(cq, mini_array, title, cqe_cnt, &wqe_cnt, cqe_idx, mp_rq, cqe_mask);
}

static inline void mlx5_decompress_curr(struct mlx5_cq *cq, uint32_t cqe_idx, int cqe_mask)
	__attribute__((always_inline));
static inline void mlx5_decompress_curr(struct mlx5_cq *cq, uint32_t cqe_idx, int cqe_mask)
{
	struct mlx5_cqe64 *title, *cqe;
	struct mlx5_mini_cqe8 mini_array[8];
	int cqe_cnt = cq->compressed_left;
	uint16_t wqe_cnt = cq->compressed_wqe_cnt;
	int mp_rq = cq->compressed_mp_rq;
	int first_chunk;

	if (!cqe_cnt)
		return;

	cq->compressed_left = 0;
	title = &cq->next_decomp_cqe64;
	first_chunk = min(cqe_cnt, MLX5_MINI_ARR_SIZE - cq->mini_arr_idx);
	copy_cqes(cq, &cq->mini_array[cq->mini_arr_idx], title, first_chunk,
		  &wqe_cnt, cqe_idx, mp_rq, cqe_mask);
	cqe_cnt -= first_chunk;
	if (!cqe_cnt)
		return;
	cqe_idx = cqe_idx + first_chunk;

	cqe = get_cqe(cq, cqe_idx & cqe_mask);
	memcpy(mini_array, cqe, sizeof(*title));
	for (; cqe_cnt > MLX5_MINI_ARR_SIZE - 1;
	     cqe_idx += MLX5_MINI_ARR_SIZE, cqe_cnt -= MLX5_MINI_ARR_SIZE) {
		copy_cqes(cq, mini_array, title, MLX5_MINI_ARR_SIZE, &wqe_cnt,
			  cqe_idx, mp_rq, cqe_mask);
		cqe = get_cqe(cq, (cqe_idx + MLX5_MINI_ARR_SIZE) &
				  cqe_mask);
		memcpy(mini_array, cqe, sizeof(*title));
	}

	copy_cqes(cq, mini_array, title, cqe_cnt, &wqe_cnt, cqe_idx, mp_rq, cqe_mask);
}

static inline void update_cqe_ownership(struct mlx5_cq *cq, int size) __attribute__((always_inline));
static inline void update_cqe_ownership(struct mlx5_cq *cq, int size)
{
	uint8_t opown = (cq->cons_index >> (cq->cq_log_size)) & 1;
	uint32_t idx = cq->cons_index & cq->ibv_cq.cqe;
	struct mlx5_cqe64 *cqe = get_cqe(cq, idx);
	int step_size = cq->cqe_sz / sizeof(*cqe);
	uint32_t last_idx;

	last_idx = idx + size;
	while ((idx < last_idx) && (idx <= cq->ibv_cq.cqe)) {
		cqe->op_own = opown;
		cqe += step_size;
		idx++;
	}
	if (unlikely(idx < last_idx)) {
		cqe = get_cqe(cq, 0);
		while (idx < last_idx) {
			cqe->op_own = !opown;
			cqe += step_size;
			idx++;
		}
	}

}

static inline void save_mini_arr(struct mlx5_cq *cq, uint32_t cqe_idx) __attribute__((always_inline));
static inline void save_mini_arr(struct mlx5_cq *cq, uint32_t cqe_idx)
{
	struct mlx5_cqe64 *cqe = get_cqe(cq, cqe_idx & cq->ibv_cq.cqe);

	memcpy(&cq->mini_array, cqe, sizeof(cq->mini_array));
	cq->mini_arr_idx = 0;
	update_cqe_ownership(cq, min(cq->compressed_left, MLX5_MINI_ARR_SIZE));
}

static inline void save_title(struct mlx5_cq *cq, uint32_t cqe_idx, int rx_only) __attribute__((always_inline));
static inline void save_title(struct mlx5_cq *cq, uint32_t cqe_idx, int rx_only)
{
	struct mlx5_cqe64 *cqe = get_cqe(cq, cqe_idx & cq->ibv_cq.cqe);

	memcpy(&cq->next_decomp_cqe64, cqe, sizeof(cq->next_decomp_cqe64));
	cq->compressed_left = ntohl(cq->next_decomp_cqe64.byte_cnt);
	cq->compressed_req = is_requestor(cq->next_decomp_cqe64.op_own >> 4);
	cq->compressed_wqe_cnt = ntohs(cq->next_decomp_cqe64.wqe_counter);
	if (unlikely(rx_only && cq->compressed_req))
		cq->compressed_rsc = NULL;
	else
		cq->compressed_rsc = find_rsc(cq, &cq->next_decomp_cqe64, (to_mctx(cq->ibv_cq.context))->cqe_version);
	cq->compressed_mp_rq = cq->compressed_rsc ? cq->compressed_rsc->type == MLX5_RSC_TYPE_MP_RWQ : 0;
}

static inline void decomp_next_cqe(struct mlx5_cq *cq) __attribute__((always_inline));
static inline void decomp_next_cqe(struct mlx5_cq *cq)
{
	struct mlx5_mini_cqe8 *mini_ent;

	if (unlikely(cq->mini_arr_idx == MLX5_MINI_ARR_SIZE))
		save_mini_arr(cq, cq->cons_index);
	mini_ent = &cq->mini_array[cq->mini_arr_idx];
	cq->next_decomp_cqe64.byte_cnt = mini_ent->byte_cnt;
	if (cq->compressed_req) {
		cq->next_decomp_cqe64.wqe_counter = mini_ent->s_wqe_info.wqe_counter;
		cq->next_decomp_cqe64.sop_qpn.sop = mini_ent->s_wqe_info.s_wqe_opcode;
	} else {
		/* for now we are supporting only rx_hash_res not
		 * checksum */
		cq->next_decomp_cqe64.rx_hash_res = mini_ent->rx_hash_result;
		cq->next_decomp_cqe64.wqe_counter = htons(cq->compressed_wqe_cnt);
		if (cq->compressed_mp_rq)
			/*
			 * In case of mp_rq the wqe_cnt is the stride index of the message start,
			 * therefore we need to increase it by the number of consumed strides
			 */
			cq->compressed_wqe_cnt += (ntohl(mini_ent->byte_cnt) & MP_RQ_NUM_STRIDES_FIELD_MASK) >>
						  MP_RQ_NUM_STRIDES_FIELD_SHIFT;
		else
			/*
			 * In case of non mp_rq the wqe_cnt is the sq/rq wqe counter,
			 * therefore we need to increase it by one
			 */
			cq->compressed_wqe_cnt++;
	}
	cq->mini_arr_idx++;
	cq->compressed_left--;
}

static inline void acc_rx_decomp_next_cqe(struct mlx5_cq *cq, uint32_t *byte_cnt) __attribute__((always_inline));
static inline void acc_rx_decomp_next_cqe(struct mlx5_cq *cq, uint32_t *byte_cnt)
{
	struct mlx5_mini_cqe8 *mini_ent;

	if (unlikely(cq->mini_arr_idx == MLX5_MINI_ARR_SIZE))
		save_mini_arr(cq, cq->cons_index);
	mini_ent = &cq->mini_array[cq->mini_arr_idx];
	*byte_cnt = ntohl(mini_ent->byte_cnt);
	cq->next_decomp_cqe64.wqe_counter = htons(cq->compressed_wqe_cnt);
	if (cq->compressed_mp_rq)
		/*
		 * In case of mp_rq the wqe_cnt is the stride index of the message start,
		 * therefore we need to increase it by the number of consumed strides
		 */
		cq->compressed_wqe_cnt += (*byte_cnt & MP_RQ_NUM_STRIDES_FIELD_MASK) >>
					  MP_RQ_NUM_STRIDES_FIELD_SHIFT;
	else
		/*
		 * In case of non mp_rq the wqe_cnt is the sq/rq wqe counter,
		 * therefore we need to increase it by one
		 */
		cq->compressed_wqe_cnt++;
	cq->mini_arr_idx++;
	cq->compressed_left--;
}

static inline struct mlx5_cqe64 *mlx5_decompress_cqe(struct mlx5_cq *cq)
	__attribute__((always_inline));
static inline struct mlx5_cqe64 *mlx5_decompress_cqe(struct mlx5_cq *cq)
{
	if (unlikely(!cq->compressed_left)) {
		save_title(cq, cq->cons_index, 0);
		save_mini_arr(cq, cq->cons_index + 1);
	}
	decomp_next_cqe(cq);

	return &cq->next_decomp_cqe64;
}

static inline struct mlx5_cqe64 *mlx5_acc_rx_decompress_cqe(struct mlx5_cq *cq, uint32_t *byte_cnt)
	__attribute__((always_inline));
static inline struct mlx5_cqe64 *mlx5_acc_rx_decompress_cqe(struct mlx5_cq *cq, uint32_t *byte_cnt)
{
	if (unlikely(!cq->compressed_left)) {
		save_title(cq, cq->cons_index, 1);
		save_mini_arr(cq, cq->cons_index + 1);
	}
	acc_rx_decomp_next_cqe(cq, byte_cnt);

	return &cq->next_decomp_cqe64;
}

static inline void handle_tm_list_op(struct ibv_exp_wc *wc,
				     struct mlx5_cqe64 *cqe64,
				     struct mlx5_srq *srq,
				     uint64_t *exp_wc_flags)
	__attribute__((always_inline));
static inline void handle_tm_list_op(struct ibv_exp_wc *wc,
				     struct mlx5_cqe64 *cqe64,
				     struct mlx5_srq *srq,
				     uint64_t *exp_wc_flags)
{
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(srq->vsrq.srq.context)->dbg_fp;
#endif
	struct mlx5_srq_op *op;

	mlx5_spin_lock(&srq->lock);
	if (srq->op_tail == srq->op_head) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "got unexpected list op CQE\n");
		wc->status = IBV_WC_GENERAL_ERR;
		mlx5_spin_unlock(&srq->lock);
		return;
	}
	op = srq->op + (srq->op_head++ &
			(srq->cmd_qp->sq.wqe_cnt - 1));
	if (op->tag) {
		mlx5_tm_release_tag(srq, op->tag);
		if (cqe64->app_op == MLX5_CQE_APP_OP_TM_REMOVE &&
		    !(*exp_wc_flags & IBV_EXP_WC_TM_DEL_FAILED))
			mlx5_tm_release_tag(srq, op->tag);
		if (ntohs(cqe64->tm_cqe.hw_phase_cnt) !=
		    op->tag->phase_cnt)
			*exp_wc_flags |= IBV_EXP_WC_TM_SYNC_REQ;
	}

	srq->cmd_qp->sq.tail = op->wqe_head;
	wc->wr_id = op->wr_id;

	mlx5_spin_unlock(&srq->lock);
}

static const uint64_t tm_wc_flags[] = {
	[MLX5_CQE_APP_OP_TM_EXPECTED]		  = IBV_EXP_WC_TM_DATA_VALID,
	[MLX5_CQE_APP_OP_TM_CONSUMED]		  = IBV_EXP_WC_TM_MATCH,
	[MLX5_CQE_APP_OP_TM_CONSUMED_SW_RDNV]     = IBV_EXP_WC_TM_MATCH,
	[MLX5_CQE_APP_OP_TM_CONSUMED_MSG]	  = IBV_EXP_WC_TM_MATCH |
						    IBV_EXP_WC_TM_DATA_VALID,
	[MLX5_CQE_APP_OP_TM_CONSUMED_MSG_SW_RDNV] = IBV_EXP_WC_TM_MATCH |
						    IBV_EXP_WC_TM_DATA_VALID,
	[MLX5_CQE_APP_OP_TM_MSG_COMPLETION_CANCELED] = IBV_EXP_WC_TM_MATCH |
						       IBV_EXP_WC_TM_DATA_VALID,
};

static inline void handle_tag_matching(struct ibv_exp_wc *wc,
				       struct mlx5_cqe64 *cqe64,
				       struct mlx5_srq *srq,
				       uint64_t *exp_wc_flags)
	__attribute__((always_inline));
static inline void handle_tag_matching(struct ibv_exp_wc *wc,
				       struct mlx5_cqe64 *cqe64,
				       struct mlx5_srq *srq,
				       uint64_t *exp_wc_flags)
{
	/* ibv_exp_tmh was defined by design based on PRM */
	struct ibv_exp_tmh *tmh = (struct ibv_exp_tmh *)cqe64;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(srq->vsrq.srq.context)->dbg_fp;
#endif
	struct mlx5_tag_entry *tag;

	wc->status = IBV_WC_SUCCESS;
	switch (cqe64->app_op) {
	case MLX5_CQE_APP_OP_TM_CONSUMED_MSG_SW_RDNV:
	case MLX5_CQE_APP_OP_TM_CONSUMED_SW_RDNV:
	case MLX5_CQE_APP_OP_TM_MSG_COMPLETION_CANCELED:
		*exp_wc_flags |= IBV_EXP_WC_TM_RNDV_INCOMPLETE;
		/* fall through */

	case MLX5_CQE_APP_OP_TM_CONSUMED_MSG:
	case MLX5_CQE_APP_OP_TM_CONSUMED:
	case MLX5_CQE_APP_OP_TM_EXPECTED:
		mlx5_spin_lock(&srq->lock);
		wc->byte_len = ntohl(cqe64->byte_cnt);
		tag = &srq->tm_list[ntohs(cqe64->app_info)];
		if (!tag->expect_cqe) {
			mlx5_dbg(fp, MLX5_DBG_CQ, "got idx %d which wasn't added\n",
				 ntohs(cqe64->app_info));
			wc->status = IBV_WC_GENERAL_ERR;
			mlx5_spin_unlock(&srq->lock);
			return;
		}
		wc->wr_id = tag->wr_id;
		wc->exp_opcode = IBV_EXP_WC_TM_RECV;
		wc->tm_info.tag = ntohll(tmh->tag);
		wc->tm_info.priv = ntohl(tmh->app_ctx);
		*exp_wc_flags |= tm_wc_flags[cqe64->app_op];
		if (cqe64->app_op != MLX5_CQE_APP_OP_TM_CONSUMED &&
		    cqe64->app_op != MLX5_CQE_APP_OP_TM_CONSUMED_SW_RDNV)
			mlx5_tm_release_tag(srq, tag);
		/* inline scatter 32 not supported for TM */
		if (cqe64->op_own & MLX5_INLINE_SCATTER_64) {
			if (ntohl(cqe64->byte_cnt) > tag->size)
				wc->status = IBV_WC_LOC_LEN_ERR;
			else
				memcpy(tag->ptr, cqe64 - 1,
				       ntohl(cqe64->byte_cnt));
		}
		if ((cqe64->op_own >> 4) == MLX5_CQE_RESP_SEND_IMM) {
			*exp_wc_flags |= IBV_EXP_WC_WITH_IMM;
			wc->imm_data = cqe64->imm_inval_pkey;
		}

		mlx5_spin_unlock(&srq->lock);
		break;

	case MLX5_CQE_APP_OP_TM_APPEND:
		wc->exp_opcode = IBV_EXP_WC_TM_ADD;
		handle_tm_list_op(wc, cqe64, srq, exp_wc_flags);
		break;

	case MLX5_CQE_APP_OP_TM_REMOVE:
		wc->exp_opcode = IBV_EXP_WC_TM_DEL;
		if (!(ntohl(cqe64->tm_cqe.success) & MLX5_TMC_SUCCESS))
			*exp_wc_flags |= IBV_EXP_WC_TM_DEL_FAILED;
		handle_tm_list_op(wc, cqe64, srq, exp_wc_flags);
		break;

	case MLX5_CQE_APP_OP_TM_NOOP:
		wc->exp_opcode = IBV_EXP_WC_TM_SYNC;
		handle_tm_list_op(wc, cqe64, srq, exp_wc_flags);
		break;

	case MLX5_CQE_APP_OP_TM_NO_TAG:
		wc->status = handle_responder((struct ibv_wc *)wc, cqe64, NULL,
					      srq, MLX5_RSC_TYPE_XSRQ,
					      exp_wc_flags);
		wc->exp_opcode = IBV_EXP_WC_TM_NO_TAG;
		break;

	case MLX5_CQE_APP_OP_TM_UNEXPECTED:
		srq->unexp_in++;
		if (srq->unexp_in - srq->unexp_out > MLX5_TM_MAX_SYNC_DIFF)
			*exp_wc_flags |= IBV_EXP_WC_TM_SYNC_REQ;

		wc->status = handle_responder((struct ibv_wc *)wc, cqe64, NULL,
					      srq, MLX5_RSC_TYPE_XSRQ,
					      exp_wc_flags);
		wc->exp_opcode = IBV_EXP_WC_TM_RECV;
		break;
	}
}

static inline int mlx5_poll_one(struct mlx5_cq *cq,
				struct mlx5_resource **cur_rsc,
				struct mlx5_srq **cur_srq, struct ibv_exp_wc *wc,
				uint32_t wc_size,
				int cqe_ver) __attribute__((always_inline));
static inline int mlx5_poll_one(struct mlx5_cq *cq,
				struct mlx5_resource **cur_rsc,
				struct mlx5_srq **cur_srq,
				struct ibv_exp_wc *wc,
				uint32_t wc_size,
				int cqe_ver)
{
  // assert(cqe_ver == 1);  // Commented for perf, but true
  // assert(*cur_srq == NULL);  // Commented for perf, but true

	struct mlx5_cqe64 *cqe64;
	struct mlx5_wq *wq;
	uint16_t wqe_ctr;
	uint32_t rsn;
	int idx;
	uint8_t opcode;
	int requestor;
	int responder;
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);
	struct mlx5_qp *mqp = NULL;
	uint64_t exp_wc_flags = 0;
	enum mlx5_rsc_type type = MLX5_RSC_TYPE_INVAL;

	cqe64 = get_next_cqe(cq, cq->cqe_sz);
	if (!cqe64)
		return CQ_EMPTY;

	++cq->cons_index;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

#if 0
	if (mlx5_debug_mask & MLX5_DBG_CQ_CQE) {
		FILE *fp = mctx->dbg_fp;

		mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "dump cqe for cqn 0x%x:\n", cq->cqn);
		dump_cqe(fp, cqe64);
	}
#endif

	((struct ibv_wc *)wc)->wc_flags = 0;
	opcode = cqe64->op_own >> 4;
	requestor = is_requestor(opcode);
	responder = is_responder(opcode);
	if (unlikely(!requestor && !responder))
		return CQ_POLL_ERR;

	rsn = ntohl(cqe64->sop_drop_qpn) & 0xffffff;
	if (1) {
		if (!*cur_rsc) {
			*cur_rsc = mlx5_find_uidx(mctx, 0);
			if (unlikely(!*cur_rsc))
				return CQ_POLL_ERR;
		}
	} else {
#if 0
		if (responder && srqn_uidx) {
			is_srq = 1;
			if (!*cur_srq || (srqn_uidx != (*cur_srq)->srqn)) {
				*cur_srq = mlx5_find_srq(mctx, srqn_uidx);
				if (unlikely(!*cur_srq))
					return CQ_POLL_ERR;
			}
		}

		if (!*cur_rsc || (rsn != (*cur_rsc)->rsn)) {
			*cur_rsc = mlx5_find_rsc(mctx, rsn);
			if (unlikely(!*cur_rsc && !srqn_uidx))
				return CQ_POLL_ERR;
		}
#endif
	}

	if (*cur_rsc) {
		switch ((*cur_rsc)->type) {
		case MLX5_RSC_TYPE_QP:
			mqp = (struct mlx5_qp *)*cur_rsc;
			if (likely(offsetof(struct ibv_exp_wc, qp) < wc_size)) {
				wc->qp = &mqp->verbs_qp.qp;
				exp_wc_flags |= IBV_EXP_WC_QP;
			}
			if (cqe_ver && responder && 0) {
				*cur_srq = to_msrq(mqp->verbs_qp.qp.srq);
			}
			break;
#if 0  // Unused queue types
		case MLX5_RSC_TYPE_DCT:
			mdct = (struct mlx5_dct *)*cur_rsc;
			is_srq = 1;
			if (likely(offsetof(struct ibv_exp_wc, dct) < wc_size)) {
				wc->dct = &mdct->ibdct;
				exp_wc_flags |= IBV_EXP_WC_DCT;
			}

			if (cqe_ver)
				*cur_srq = to_msrq(mdct->ibdct.srq);
			break;
		case MLX5_RSC_TYPE_XSRQ:
			*cur_srq = (struct mlx5_srq *)*cur_rsc;
			is_srq = 1;
			break;
		case MLX5_RSC_TYPE_RWQ:
		case MLX5_RSC_TYPE_MP_RWQ:
			rwq = (struct mlx5_rwq *)*cur_rsc;
			break;
#endif
		default:
			return CQ_POLL_ERR;
		}
		type = (*cur_rsc)->type;
	}

	if (0 && likely(offsetof(struct ibv_exp_wc, srq) < wc_size)) {
		wc->srq = &(*cur_srq)->vsrq.srq;
		exp_wc_flags |= IBV_EXP_WC_SRQ;
	}

	wc->qp_num = rsn;

	switch (opcode) {
	case MLX5_CQE_REQ:
    // post_send() completion
		if (0) {
			fprintf(stderr, "all requestors are kinds of QPs\n");
			return CQ_POLL_ERR;
		}
		wq = &mqp->sq;
		wqe_ctr = ntohs(cqe64->wqe_counter);
		idx = wqe_ctr & (wq->wqe_cnt - 1);
		//handle_good_req((struct ibv_wc *)wc, cqe64, wq, idx); // Unused wc fields
#if 0
		if (cqe_format == MLX5_INLINE_DATA32_SEG) {
			cqe = cqe64;
			err = mlx5_copy_to_send_wqe(mqp, wqe_ctr, cqe,
						    wc->byte_len);
		} else if (cqe_format == MLX5_INLINE_DATA64_SEG) {
			cqe = cqe64 - 1;
			err = mlx5_copy_to_send_wqe(mqp, wqe_ctr, cqe,
						    wc->byte_len);
		} else {
			err = 0;
		}
#endif

		wc->wr_id = wq->wrid[idx];
		wq->tail = mqp->gen_data.wqe_head[idx] + 1;
		wc->status = 0;
		break;
	case MLX5_CQE_RESP_SEND:
    // post_recv() completion
		handle_responder((struct ibv_wc *)wc, cqe64, mqp, NULL, type, &exp_wc_flags);
		break;

#if 0  // Unsupported CQE types
	case MLX5_CQE_RESP_SEND_IMM:
	case MLX5_CQE_RESP_SEND_INV:
		if (cqe64->app == MLX5_CQE_APP_TAG_MATCHING) {
			if (!is_srq)
				return CQ_POLL_ERR;

			handle_tag_matching(wc, cqe64, *cur_srq, &exp_wc_flags);
			break;
		}

		wc->status = handle_responder((struct ibv_wc *)wc, cqe64, mqp,
					      is_srq ? *cur_srq : NULL, type,
					      &exp_wc_flags);
		if (mqp &&
		    (mqp->gen_data.model_flags & MLX5_QP_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP)) {
			l3_hdr = (cqe64->l4_hdr_type_etc) & MLX5_CQE_L3_HDR_TYPE_MASK;
			exp_wc_flags |=
				(!!(cqe64->hds_ip_ext & MLX5_CQE_L4_OK) *
				 (uint64_t)IBV_EXP_WC_RX_TCP_UDP_CSUM_OK) |
				(!!(cqe64->hds_ip_ext & MLX5_CQE_L3_OK) *
				 (uint64_t)IBV_EXP_WC_RX_IP_CSUM_OK) |
				((l3_hdr == MLX5_CQE_L3_HDR_TYPE_IPV4) *
				 (uint64_t)IBV_EXP_WC_RX_IPV4_PACKET) |
				((l3_hdr == MLX5_CQE_L3_HDR_TYPE_IPV6) *
				 (uint64_t)IBV_EXP_WC_RX_IPV6_PACKET);
		}
		break;

	case MLX5_CQE_NO_PACKET:
		if (cqe64->app != MLX5_CQE_APP_TAG_MATCHING || !is_srq)
			return CQ_POLL_ERR;
		handle_tag_matching(wc, cqe64, *cur_srq, &exp_wc_flags);
		break;

	case MLX5_CQE_RESIZE_CQ:
		break;
	case MLX5_CQE_REQ_ERR:
	case MLX5_CQE_RESP_ERR:
		{
		uint8_t is_umr = 0;
		ecqe = (struct mlx5_err_cqe *)cqe64;
		mlx5_handle_error_cqe(ecqe, wc);
		mlx5_set_bad_wc_opcode(wc, ecqe, (opcode == MLX5_CQE_REQ_ERR), &is_umr);
		if (unlikely(ecqe->syndrome != MLX5_CQE_SYNDROME_WR_FLUSH_ERR &&
			     ecqe->syndrome != MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR)) {
			FILE *fp = mctx->dbg_fp;
			fprintf(fp, PFX "%s: got completion with error:\n",
				mctx->hostname);
			dump_cqe(fp, ecqe);
			if (mlx5_freeze_on_error_cqe) {
				fprintf(fp, PFX "freezing at poll cq...");
				while (1)
					sleep(10);
			}
		}

		if (opcode == MLX5_CQE_REQ_ERR) {
			wq = &mqp->sq;
			wqe_ctr = ntohs(cqe64->wqe_counter);
			idx = wqe_ctr & (wq->wqe_cnt - 1);
			wc->wr_id = wq->wrid[idx];
			wq->tail = mqp->gen_data.wqe_head[idx] + 1;
			if (is_umr)
				wc->exp_opcode = wq->wr_data[idx];
		} else {
			if (*cur_srq) {
				wqe_ctr = ntohs(cqe64->wqe_counter);
				wc->wr_id = (*cur_srq)->wrid[wqe_ctr];
				mlx5_free_srq_wqe(*cur_srq, wqe_ctr);
			} else {
				if (rwq)
					wq = &rwq->rq;
				else
					wq = &mqp->rq;
				wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
				++wq->tail;
			}
		}
		break;
#endif
  default:
    return CQ_POLL_ERR;

	}

#if 0  // Unneeded WC flags
	if (unlikely(timestamp_en)) {
		wc->timestamp = ntohll(cqe64->timestamp);
		exp_wc_flags |= IBV_EXP_WC_WITH_TIMESTAMP;
	}

	if (likely(offsetof(struct ibv_exp_wc, exp_wc_flags) < wc_size))
		wc->exp_wc_flags = exp_wc_flags | (uint64_t)((struct ibv_wc *)wc)->wc_flags;

	if (unlikely(cq->peer_enabled &&
	    !(cq->peer_ctx->caps & IBV_EXP_PEER_OP_POLL_NOR_DWORD_CAP))) {
		cqe64->op_own = MLX5_CQE_INVALID << 4;
		wmb();
	}
#endif

	return CQ_OK;
}

int mlx5_exp_peer_peek_cq(struct ibv_cq *ibcq,
			  struct ibv_exp_peer_peek *peek_ctx)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct peer_op_wr *wr = peek_ctx->storage;
	struct mlx5_peek_entry *peek;
	int entries = 2;
	int n, cur_own;
	void *cqe;
	struct mlx5_cqe64 *cqe64;

	if (!cq->peer_enabled)
		return EINVAL;

	if (peek_ctx->entries < entries)
		return ENOSPC;

	mlx5_lock(&cq->lock);
	switch (peek_ctx->whence) {
	case IBV_EXP_PEER_PEEK_ABSOLUTE:
		if (peek_ctx->offset > cq->cons_index + cq->ibv_cq.cqe) {
			mlx5_unlock(&cq->lock);
			return E2BIG;
		}
		n = peek_ctx->offset;
		break;
	case IBV_EXP_PEER_PEEK_RELATIVE:
		if (peek_ctx->offset > cq->ibv_cq.cqe) {
			mlx5_unlock(&cq->lock);
			return E2BIG;
		}
		n = cq->cons_index + peek_ctx->offset - 1;
		break;
	default:
		mlx5_unlock(&cq->lock);
		return EINVAL;
	}
	cqe = cq->active_buf->buf + (n & cq->ibv_cq.cqe) * cq->cqe_sz;
	cur_own = n & (cq->ibv_cq.cqe + 1);
	cqe64 = (cq->cqe_sz == 64) ? cqe : cqe + 64;

	if (cur_own) {
		wr->type = IBV_EXP_PEER_OP_POLL_AND_DWORD;
		wr->wr.dword_va.data = htonl(MLX5_CQE_OWNER_MASK);
	} else if (cq->peer_ctx->caps & IBV_EXP_PEER_OP_POLL_NOR_DWORD_CAP) {
		wr->type = IBV_EXP_PEER_OP_POLL_NOR_DWORD;
		wr->wr.dword_va.data = ~htonl(MLX5_CQE_OWNER_MASK);
	} else if (cq->peer_ctx->caps & IBV_EXP_PEER_OP_POLL_GEQ_DWORD_CAP) {
		wr->type = IBV_EXP_PEER_OP_POLL_GEQ_DWORD;
		wr->wr.dword_va.data = 0;
	}
	wr->wr.dword_va.target_id = cq->active_buf->peer.va_id;
	wr->wr.dword_va.offset = (uintptr_t)&cqe64->wqe_counter -
				 (uintptr_t)cq->active_buf->buf;
	wr = wr->next;

	peek = cq->peer_peek_free;
	if (!peek) {
		mlx5_unlock(&cq->lock);
		return ENOMEM;
	}
	cq->peer_peek_free = PEEK_ENTRY(cq, peek->next);
	peek->busy = 1;
	peek->next = PEEK_ENTRY_N(cq, cq->peer_peek_table[n & cq->ibv_cq.cqe]);
	cq->peer_peek_table[n & cq->ibv_cq.cqe] = peek;

	wr->type = IBV_EXP_PEER_OP_STORE_DWORD;
	wr->wr.dword_va.data = 0;
	wr->wr.dword_va.target_id = cq->peer_buf.peer.va_id;
	wr->wr.dword_va.offset = (uintptr_t)&peek->busy -
				 (uintptr_t)cq->peer_buf.buf;

	peek_ctx->entries = entries;
	peek_ctx->peek_id = (uintptr_t)peek;
	mlx5_unlock(&cq->lock);

	return 0;
}

int mlx5_exp_peer_abort_peek_cq(struct ibv_cq *ibcq,
				struct ibv_exp_peer_abort_peek *peek_ctx)
{
	struct mlx5_cq *cq = to_mcq(ibcq);

	if (!cq->peer_enabled)
		return EINVAL;

	((struct mlx5_peek_entry *)(uintptr_t)peek_ctx->peek_id)->busy = 0;
	return 0;
}

static inline int poll_cq(struct ibv_cq *ibcq, int ne, struct ibv_exp_wc *wc,
			  uint32_t wc_size, int cqe_ver) __attribute__((always_inline));
static inline int poll_cq(struct ibv_cq *ibcq, int ne, struct ibv_exp_wc *wc,
			  uint32_t wc_size, int cqe_ver)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_resource *rsc = NULL;
	struct mlx5_srq *srq = NULL;
	int npolled;
	int err = CQ_OK;
	void *twc;

	if (0) {
		if (cq->stall_adaptive_enable) {
			if (cq->stall_last_count)
				mlx5_stall_cycles_poll_cq(cq->stall_last_count + cq->stall_cycles);
		} else if (cq->stall_next_poll) {
			cq->stall_next_poll = 0;
			mlx5_stall_poll_cq();
		}
	}


	for (npolled = 0, twc = wc; npolled < ne; ++npolled, twc += wc_size) {
		err = mlx5_poll_one(cq, &rsc, &srq, twc, wc_size, cqe_ver);
		if (err != CQ_OK)
			break;
	}

	mlx5_update_cons_index(cq);


	if (0) {
		if (cq->stall_adaptive_enable) {
			if (npolled == 0) {
				cq->stall_cycles = max(cq->stall_cycles-mlx5_stall_cq_dec_step,
						       mlx5_stall_cq_poll_min);
				mlx5_get_cycles(&cq->stall_last_count);
			} else if (npolled < ne) {
				cq->stall_cycles = min(cq->stall_cycles+mlx5_stall_cq_inc_step,
						       mlx5_stall_cq_poll_max);
				mlx5_get_cycles(&cq->stall_last_count);
			} else {
				cq->stall_cycles = max(cq->stall_cycles-mlx5_stall_cq_dec_step,
						       mlx5_stall_cq_poll_min);
				cq->stall_last_count = 0;
			}
		} else if (err == CQ_EMPTY) {
			cq->stall_next_poll = 1;
		}
	}

	return err == CQ_POLL_ERR ? err : npolled;
}

int mlx5_poll_cq(struct ibv_cq *ibcq, int ne, struct ibv_wc *wc)
{
	return poll_cq(ibcq, ne, (struct ibv_exp_wc *)wc, sizeof(*wc), 0);
}

int mlx5_poll_cq_1(struct ibv_cq *ibcq, int ne, struct ibv_wc *wc)
{
	return poll_cq(ibcq, ne, (struct ibv_exp_wc *)wc, sizeof(*wc), 1);
}

int mlx5_poll_cq_ex(struct ibv_cq *ibcq, int ne,
		    struct ibv_exp_wc *wc, uint32_t wc_size)
{
	return poll_cq(ibcq, ne, wc, wc_size, 0);
}

int mlx5_poll_cq_ex_1(struct ibv_cq *ibcq, int ne,
		      struct ibv_exp_wc *wc, uint32_t wc_size)
{
	return poll_cq(ibcq, ne, wc, wc_size, 1);
}

int mlx5_arm_cq(struct ibv_cq *ibvcq, int solicited)
{
	struct mlx5_cq *cq = to_mcq(ibvcq);
	struct mlx5_context *ctx = to_mctx(ibvcq->context);
	uint32_t doorbell[2];
	uint32_t sn;
	uint32_t ci;
	uint32_t cmd;

	sn  = cq->arm_sn & 3;
	ci  = cq->cons_index & 0xffffff;
	cmd = solicited ? MLX5_CQ_DB_REQ_NOT_SOL : MLX5_CQ_DB_REQ_NOT;

	cq->dbrec[MLX5_CQ_ARM_DB] = htonl(sn << 28 | cmd | ci);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = htonl(sn << 28 | cmd | ci);
	doorbell[1] = htonl(cq->cqn);

	mlx5_write64(doorbell, ctx->uar[0].regs + MLX5_CQ_DOORBELL, &ctx->lock32);

	wc_wmb();

	return 0;
}

void mlx5_cq_event(struct ibv_cq *cq)
{
	to_mcq(cq)->arm_sn++;
}

static int is_equal_rsn(struct mlx5_cqe64 *cqe64, uint32_t rsn)
{
	return rsn == (ntohl(cqe64->sop_drop_qpn) & 0xffffff);
}

static int is_equal_uidx(struct mlx5_cqe64 *cqe64, uint32_t uidx)
{
	return uidx == (ntohl(cqe64->srqn_uidx) & 0xffffff);
}

static inline int free_res_cqe(struct mlx5_cqe64 *cqe64, uint32_t rsn_uidx,
			       struct mlx5_srq *srq, int cqe_version)
{
	if (cqe_version) {
		if (is_equal_uidx(cqe64, rsn_uidx)) {
			if (srq && is_responder(cqe64->op_own >> 4))
				mlx5_free_srq_wqe(srq,
						  ntohs(cqe64->wqe_counter));
			return 1;
		}
	} else {
		if (is_equal_rsn(cqe64, rsn_uidx)) {
			if (srq && (ntohl(cqe64->srqn_uidx) & 0xffffff))
				mlx5_free_srq_wqe(srq,
						  ntohs(cqe64->wqe_counter));
			return 1;
		}
	}

	return 0;
}

void __mlx5_cq_clean(struct mlx5_cq *cq, uint32_t rsn_uidx, struct mlx5_srq *srq)
{
	uint32_t prod_index;
	int nfreed = 0;
	struct mlx5_cqe64 *cqe64, *dest64;
	void *cqe, *dest;
	uint8_t owner_bit;
	int cqe_version;

	if (!cq || cq->model_flags & MLX5_CQ_MODEL_FLAG_DV_OWNED)
		return;

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	cqe_version = (to_mctx(cq->ibv_cq.context))->cqe_version;
	mlx5_decompress_curr(cq, cq->cons_index, cq->ibv_cq.cqe);
	for (prod_index = cq->cons_index; (cqe = get_sw_cqe(cq, prod_index, cq->ibv_cq.cqe)); ++prod_index) {
		if (mlx5_get_cqe_format(cqe) == MLX5_COMPRESSED)
			mlx5_decompress_cqe_idx(cq, prod_index, cq->ibv_cq.cqe);

		if (prod_index == cq->cons_index + cq->ibv_cq.cqe)
			break;
	}

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibv_cq.cqe);
		cqe64 = (cq->cqe_sz == 64) ? cqe : cqe + 64;
		if (free_res_cqe(cqe64, rsn_uidx, srq, cqe_version)) {
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibv_cq.cqe);
			dest64 = (cq->cqe_sz == 64) ? dest : dest + 64;
			owner_bit = dest64->op_own & MLX5_CQE_OWNER_MASK;
			memcpy(dest, cqe, cq->cqe_sz);
			dest64->op_own = owner_bit |
				(dest64->op_own & ~MLX5_CQE_OWNER_MASK);
		}
	}

	if (nfreed) {
		cq->cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		wmb();
		mlx5_update_cons_index(cq);
	}
}

void mlx5_cq_clean(struct mlx5_cq *cq, uint32_t qpn, struct mlx5_srq *srq)
{
	mlx5_lock(&cq->lock);
	__mlx5_cq_clean(cq, qpn, srq);
	mlx5_unlock(&cq->lock);
}

static uint8_t sw_ownership_bit(int n, int nent)
{
	return (n & nent) ? 1 : 0;
}

static int is_hw(uint8_t own, int n, int mask)
{
	return (own & MLX5_CQE_OWNER_MASK) ^ !!(n & (mask + 1));
}

void mlx5_cq_resize_copy_cqes(struct mlx5_cq *cq)
{
	struct mlx5_cqe64 *scqe64;
	struct mlx5_cqe64 *dcqe64;
	void *start_cqe;
	void *scqe;
	void *dcqe;
	int ssize;
	int dsize;
	int i;
	int prod_index;
	uint8_t sw_own;

	ssize = cq->cqe_sz;
	dsize = cq->resize_cqe_sz;
	prod_index = cq->cons_index;
	mlx5_decompress_curr(cq, prod_index, cq->active_cqes);
	for (; (scqe = get_sw_cqe(cq, prod_index, cq->active_cqes)); ++prod_index) {
		if (mlx5_get_cqe_format(scqe) == MLX5_COMPRESSED)
			mlx5_decompress_cqe_idx(cq, prod_index, cq->active_cqes);

		if (prod_index == cq->cons_index + cq->active_cqes)
			break;
	}

	i = cq->cons_index;
	scqe = get_buf_cqe(cq->active_buf, i & cq->active_cqes, ssize);
	scqe64 = ssize == 64 ? scqe : scqe + 64;
	start_cqe = scqe;
	if (is_hw(scqe64->op_own, i, cq->active_cqes)) {
		fprintf(stderr, "expected cqe in sw ownership\n");
		return;
	}

	while ((scqe64->op_own >> 4) != MLX5_CQE_RESIZE_CQ) {
		dcqe = get_buf_cqe(cq->resize_buf, (i + 1) & (cq->resize_cqes - 1), dsize);
		dcqe64 = dsize == 64 ? dcqe : dcqe + 64;
		sw_own = sw_ownership_bit(i + 1, cq->resize_cqes);
		memcpy(dcqe, scqe, ssize);
		dcqe64->op_own = (dcqe64->op_own & ~MLX5_CQE_OWNER_MASK) | sw_own;

		++i;
		scqe = get_buf_cqe(cq->active_buf, i & cq->active_cqes, ssize);
		scqe64 = ssize == 64 ? scqe : scqe + 64;
		if (is_hw(scqe64->op_own, i, cq->active_cqes)) {
			fprintf(stderr, "expected cqe in sw ownership\n");
			return;
		}

		if (scqe == start_cqe) {
			fprintf(stderr, "resize CQ failed to get resize CQE\n");
			return;
		}
	}
	++cq->cons_index;
}

int mlx5_alloc_cq_buf(struct mlx5_context *mctx, struct mlx5_cq *cq,
		      struct mlx5_buf *buf, int nent, int cqe_sz)
{
	struct mlx5_cqe64 *cqe;
	int i;
	struct mlx5_device *dev = to_mdev(mctx->ibv_ctx.device);
	int ret;
	enum mlx5_alloc_type type;
	enum mlx5_alloc_type default_type = MLX5_ALLOC_TYPE_PREFER_CONTIG;

	if (mlx5_use_huge(&mctx->ibv_ctx, "HUGE_CQ"))
		default_type = MLX5_ALLOC_TYPE_HUGE;

	if (cq->peer_enabled && cq->peer_ctx->buf_alloc) {
		buf->peer.dir = IBV_EXP_PEER_DIRECTION_FROM_HCA |
			      IBV_EXP_PEER_DIRECTION_TO_PEER |
			      IBV_EXP_PEER_DIRECTION_TO_CPU;
		buf->peer.ctx = cq->peer_ctx;
	}

	mlx5_get_alloc_type(&mctx->ibv_ctx, MLX5_CQ_PREFIX, &type, default_type);

	buf->numa_req.valid = 1;
	buf->numa_req.numa_id = mlx5_cpu_local_numa();
	ret = mlx5_alloc_preferred_buf(mctx, buf,
				       align(nent * cqe_sz, dev->page_size),
				       dev->page_size,
				       type,
				       MLX5_CQ_PREFIX);

	if (ret)
		return -1;

	memset(buf->buf, 0, nent * cqe_sz);

	for (i = 0; i < nent; ++i) {
		cqe = buf->buf + i * cqe_sz;
		cqe += cqe_sz == 128 ? 1 : 0;
		cqe->op_own = MLX5_CQE_INVALID << 4;
	}

	return 0;
}

int mlx5_alloc_cq_peer_buf(struct mlx5_context *ctx, struct mlx5_cq *cq, int n)
{
	struct mlx5_device *dev = to_mdev(ctx->ibv_ctx.device);
	int ret, i;

	cq->peer_peek_table = malloc(n * sizeof(struct mlx5_peek_entry *));
	if (!cq->peer_peek_table) {
		errno = ENOMEM;
		return -1;
	}
	memset(cq->peer_peek_table, 0, n * sizeof(struct mlx5_peek_entry *));

	if (cq->peer_ctx->buf_alloc) {
		cq->peer_buf.peer.dir = IBV_EXP_PEER_DIRECTION_FROM_PEER |
					IBV_EXP_PEER_DIRECTION_TO_CPU;
		cq->peer_buf.peer.ctx = cq->peer_ctx;
	}

	ret = mlx5_alloc_preferred_buf(ctx, &cq->peer_buf,
				       n * sizeof(struct mlx5_peek_entry),
				       dev->page_size,
				       MLX5_ALLOC_TYPE_ALL,
				       MLX5_CQ_PREFIX);
	if (ret) {
		free(cq->peer_peek_table);
		return ret;
	}

	memset(cq->peer_buf.buf, 0, n * sizeof(struct mlx5_peek_entry));
	cq->peer_peek_free = cq->peer_buf.buf;
	for (i = 0; i < n - 1; i++)
		cq->peer_peek_free[i].next = i + 1;
	cq->peer_peek_free[n - 1].next = LAST_PEEK_ENTRY;

	return 0;
}

/*
 *  poll  family functions
 */
static inline int32_t poll_cnt(struct ibv_cq *ibcq, uint32_t max_entries,
			       const int use_lock, const int cqe_sz,
			       const int cqe_ver) __attribute__((always_inline));
static inline int32_t poll_cnt(struct ibv_cq *ibcq, uint32_t max_entries,
			       const int use_lock, const int cqe_sz,
			       const int cqe_ver)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_resource *cur_rsc = NULL;
	struct mlx5_cqe64 *cqe64;
	struct mlx5_qp *mqp;
	int err = CQ_OK;
	uint16_t wqe_ctr;
	int npolled;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(ibcq->context)->dbg_fp;
#endif

	if (unlikely(use_lock))
		mlx5_lock(&cq->lock);

	for (npolled = 0; npolled < max_entries; ++npolled) {
		cqe64 = get_next_cqe(cq, cqe_sz);
		if (!cqe64) {
			err = CQ_EMPTY;
			break;
		}

		if (unlikely(mlx5_get_cqe_format(cqe64) == MLX5_COMPRESSED))
			cqe64 = mlx5_decompress_cqe(cq);

		cur_rsc = find_rsc(cq, cqe64, cqe_ver);
		if (unlikely(!cur_rsc)) {
			err = CQ_POLL_ERR;
			mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "Failed to find send QP on poll_cnt\n");
			break;
		}
		mqp = (struct mlx5_qp *)cur_rsc;
		if (likely((cqe64->op_own >> 4) == MLX5_CQE_REQ)) {
			wqe_ctr = ntohs(cqe64->wqe_counter);
			mqp->sq.tail = mqp->gen_data.wqe_head[wqe_ctr & (mqp->sq.wqe_cnt - 1)] + 1;
		} else if ((cqe64->op_own >> 4) == MLX5_CQE_RESP_SEND) {
			++mqp->rq.tail;
		} else {
			err = CQ_POLL_ERR;
			if ((cqe64->op_own >> 4) == MLX5_CQE_REQ_ERR)
				mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "MLX5_CQE_REQ_ERR received on poll_cnt\n");
			else
				mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "Non requester message received on poll_cnt\n");
		}

		if (unlikely(err != CQ_OK))
			break;

		++cq->cons_index;
	}

	if (likely(npolled)) {
		mlx5_update_cons_index(cq);
		err = CQ_OK;
	}

	if (unlikely(use_lock))
		mlx5_unlock(&cq->lock);

	return err == CQ_POLL_ERR ? -1 : npolled;
}

static inline int32_t get_rx_offloads_flags(struct mlx5_cqe64 *cqe) __attribute__((always_inline));
static inline int32_t get_rx_offloads_flags(struct mlx5_cqe64 *cqe)
{
	uint8_t l3_hdr;
	uint8_t l4_hdr;
	int32_t flags;

	l3_hdr = (cqe->l4_hdr_type_etc) & MLX5_CQE_L3_HDR_TYPE_MASK;
	l4_hdr = (cqe->l4_hdr_type_etc) & MLX5_CQE_L4_HDR_TYPE_MASK;
	flags = (!!(cqe->hds_ip_ext & MLX5_CQE_L4_OK) * IBV_EXP_CQ_RX_TCP_UDP_CSUM_OK) |
		(!!(cqe->hds_ip_ext & MLX5_CQE_L3_OK) * IBV_EXP_CQ_RX_IP_CSUM_OK) |
		((l3_hdr == MLX5_CQE_L3_HDR_TYPE_IPV4) * IBV_EXP_CQ_RX_IPV4_PACKET) |
		((l3_hdr == MLX5_CQE_L3_HDR_TYPE_IPV6) * IBV_EXP_CQ_RX_IPV6_PACKET) |
		(((l4_hdr == MLX5_CQE_L4_HDR_TYPE_TCP) || (l4_hdr == MLX5_CQE_L4_HDR_TYPE_TCP_EMP_ACK) ||
		  (l4_hdr == MLX5_CQE_L4_HDR_TYPE_TCP_ACK)) * IBV_EXP_CQ_RX_TCP_PACKET) |
		((l4_hdr == MLX5_CQE_L4_HDR_TYPE_UDP) * IBV_EXP_CQ_RX_UDP_PACKET);

	return flags;
}

static inline int32_t poll_length(struct ibv_cq *ibcq, void *buf, uint32_t *inl,
				  const int use_lock, const int cqe_sz,
				  uint32_t *offset, uint32_t *flags,
				  const int cqe_ver, uint16_t *vlan_cti) __attribute__((always_inline));
static inline int32_t poll_length(struct ibv_cq *ibcq, void *buf, uint32_t *inl,
				  const int use_lock, const int cqe_sz,
				  uint32_t *offset, uint32_t *flags,
				  const int cqe_ver, uint16_t *vlan_cti)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_resource *cur_rsc = NULL;
	struct mlx5_cqe64 *cqe64;
	struct mlx5_qp *mqp = NULL;
	struct mlx5_rwq *rwq = NULL;
	int32_t size = 0;
	uint32_t byte_cnt;
	uint16_t wqe_ctr;
	int err = CQ_OK;
	int cqe_format;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(ibcq->context)->dbg_fp;
#endif

	if (unlikely(use_lock))
		mlx5_lock(&cq->lock);

	cqe64 = get_next_cqe(cq, cqe_sz);

	if (cqe64) {
		cqe_format = mlx5_get_cqe_format(cqe64);
		if (unlikely(cqe_format == MLX5_COMPRESSED)) {
			cqe64 = mlx5_acc_rx_decompress_cqe(cq, &byte_cnt);
			cqe_format = 0;
			cur_rsc = cq->compressed_rsc;
		} else {
			if (unlikely((cqe64->op_own >> 4) != MLX5_CQE_RESP_SEND)) {
				if (cqe64->op_own >> 4 == MLX5_CQE_RESP_ERR)
					mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "poll_length, CQE response error, syndrome=0x%x, vendor syndrome error=0x%x, HW syndrome 0x%x, HW syndrome type 0x%x\n",
						 ((struct mlx5_err_cqe *)cqe64)->syndrome,
						 ((struct mlx5_err_cqe *)cqe64)->vendor_err_synd,
						 ((struct mlx5_err_cqe *)cqe64)->hw_err_synd,
						 ((struct mlx5_err_cqe *)cqe64)->hw_synd_type);
				else
					mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "Only post-receive completion supported on poll_length, op=%u\n",
						 cqe64->op_own >> 4);
				err = CQ_POLL_ERR;
				goto out;
			}
			cur_rsc = find_rsc(cq, cqe64, cqe_ver);
			byte_cnt = ntohl(cqe64->byte_cnt);
		}
		if (unlikely(!cur_rsc)) {
			mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "Failed to find QP resource on poll_length\n");
			err = CQ_POLL_ERR;
			goto out;
		}

		if (cur_rsc->type == MLX5_RSC_TYPE_MP_RWQ) {
			uint16_t wqe_id;

			if (unlikely(!offset)) {
				mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "Can't handle Multi-Packet RQ completion since 'offset' output parameter is not provided\n");
				err = CQ_POLL_ERR;
				goto out;
			}
			rwq = (struct mlx5_rwq *)cur_rsc;

			wqe_id = ntohs(cqe64->wqe_id) & (rwq->rq.wqe_cnt - 1);
			/* Add the WQE strides consumed by this CQE to the WQE consumed strides counter */
			rwq->consumed_strides_counter[wqe_id] += (byte_cnt & MP_RQ_NUM_STRIDES_FIELD_MASK) >>
								 MP_RQ_NUM_STRIDES_FIELD_SHIFT;

			/* Updae RX offload flags */
			if (rwq->model_flags & MLX5_WQ_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP)
				*flags = get_rx_offloads_flags(cqe64);
			else
				*flags = 0;
			/* If last packet for receive WR (all strides of this WQE consumed) */
			if (rwq->consumed_strides_counter[wqe_id] == rwq->mp_rq_strides_in_wqe) {
				*flags |= IBV_EXP_CQ_RX_MULTI_PACKET_LAST_V1;
				++rwq->rq.tail; /* Update the rq tail */
				rwq->consumed_strides_counter[wqe_id] = 0;
			}

			if (byte_cnt & MP_RQ_FILLER_FIELD_MASK)
				/*
				 * In case of filler CQE the application get WC with message-size = 0.
				 * filler CQE may come at any time regardless to the last-packet indication.
				 */
				 size = 0;
			else /* not a filler CQE */
				size = (byte_cnt & MP_RQ_BYTE_CNT_FIELD_MASK) - rwq->mp_rq_packet_padding;

			/*
			 * In mp_rq wqe_counter provides the WQE stride index.
			 * We use it to calculate packet offset in the WR posted buffer.
			 */
			*offset = ntohs(cqe64->wqe_counter) * rwq->mp_rq_stride_size + rwq->mp_rq_packet_padding;
		} else {
			if (cur_rsc->type == MLX5_RSC_TYPE_QP) {
				mqp = (struct mlx5_qp *)cur_rsc;
				if (flags) {
					if (mqp->gen_data.model_flags & MLX5_QP_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP)
						*flags = get_rx_offloads_flags(cqe64);
					else
						*flags = 0;
				}
			} else {
				if (likely(cur_rsc->type == MLX5_RSC_TYPE_RWQ)) {
					rwq = (struct mlx5_rwq *)cur_rsc;
				} else {
					mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "Invalid resource type(%d) on poll_length\n", cur_rsc->type);
					err = CQ_POLL_ERR;
					goto out;
				}
				if (flags) {
					if (rwq->model_flags & MLX5_WQ_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP)
						*flags = get_rx_offloads_flags(cqe64);
					else
						*flags = 0;
				}
			}

			size = byte_cnt;

			if (unlikely(cqe_format)) {
				void *data = (cqe_format == MLX5_INLINE_DATA32_SEG) ? cqe64 : cqe64 - 1;

				if (buf) {
					*inl = 1;
					memcpy(buf, data, size);
				} else {
					wqe_ctr = mqp->rq.tail & (mqp->rq.wqe_cnt - 1);
					if (unlikely(mlx5_copy_to_recv_wqe(mqp, wqe_ctr, data, size))) {
						mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "Fail to copy inline receive message to receive buffer\n");
						err = CQ_POLL_ERR;
						goto out;
					}
				}
			}

			if (!rwq)
				++mqp->rq.tail;
			else
				++rwq->rq.tail;
		}

		/* if CVLAN stripping is enabled, check the CQE CV bit */
		if (vlan_cti) {
		       if (cqe64->l4_hdr_type_etc & 0x1) {
				*vlan_cti = ntohs(cqe64->vlan_info);
				*flags |= IBV_EXP_CQ_RX_CVLAN_STRIPPED_V1;
		       }
		}

		++cq->cons_index;
		mlx5_update_cons_index(cq);
	} else {
		err = CQ_EMPTY;
		if (flags)
			*flags = 0;
	}

out:
	if (unlikely(use_lock))
		mlx5_unlock(&cq->lock);

	return err == CQ_POLL_ERR ? -1 : size;
}

int32_t mlx5_poll_cnt_safe(struct ibv_cq *ibcq, uint32_t max) __MLX5_ALGN_F__;
int32_t mlx5_poll_cnt_safe(struct ibv_cq *ibcq, uint32_t max)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);

	return poll_cnt(ibcq, max, 1, cq->cqe_sz, mctx->cqe_version == 1);
}

int32_t mlx5_poll_cnt_unsafe_cqe64(struct ibv_cq *ibcq, uint32_t max) __MLX5_ALGN_F__;
int32_t mlx5_poll_cnt_unsafe_cqe64(struct ibv_cq *ibcq, uint32_t max)
{
	return poll_cnt(ibcq, max, 0, 64, 0);
}

int32_t mlx5_poll_cnt_unsafe_cqe128(struct ibv_cq *ibcq, uint32_t max) __MLX5_ALGN_F__;
int32_t mlx5_poll_cnt_unsafe_cqe128(struct ibv_cq *ibcq, uint32_t max)
{
	return poll_cnt(ibcq, max, 0, 128, 0);
}

int32_t mlx5_poll_cnt_unsafe_cqe64_v1(struct ibv_cq *ibcq, uint32_t max) __MLX5_ALGN_F__;
int32_t mlx5_poll_cnt_unsafe_cqe64_v1(struct ibv_cq *ibcq, uint32_t max)
{
	return poll_cnt(ibcq, max, 0, 64, 1);
}

int32_t mlx5_poll_cnt_unsafe_cqe128_v1(struct ibv_cq *ibcq, uint32_t max) __MLX5_ALGN_F__;
int32_t mlx5_poll_cnt_unsafe_cqe128_v1(struct ibv_cq *ibcq, uint32_t max)
{
	return poll_cnt(ibcq, max, 0, 128, 1);
}

int32_t mlx5_poll_length_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);

	return poll_length(ibcq, buf, inl, 1, cq->cqe_sz, NULL, NULL,
			   mctx->cqe_version == 1, NULL);
}

int32_t mlx5_poll_length_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl)
{
	return poll_length(cq, buf, inl, 0, 64, NULL, NULL, 0, NULL);
}

int32_t mlx5_poll_length_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl)
{
	return poll_length(cq, buf, inl, 0, 128, NULL, NULL, 0, NULL);
}

int32_t mlx5_poll_length_unsafe_cqe64_v1(struct ibv_cq *cq, void *buf, uint32_t *inl) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_unsafe_cqe64_v1(struct ibv_cq *cq, void *buf, uint32_t *inl)
{
	return poll_length(cq, buf, inl, 0, 64, NULL, NULL, 1, NULL);
}

int32_t mlx5_poll_length_unsafe_cqe128_v1(struct ibv_cq *cq, void *buf, uint32_t *inl) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_unsafe_cqe128_v1(struct ibv_cq *cq, void *buf, uint32_t *inl)
{
	return poll_length(cq, buf, inl, 0, 128, NULL, NULL, 1, NULL);
}

/* Poll length flags */
int32_t mlx5_poll_length_flags_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);

	return poll_length(ibcq, buf, inl, 1, cq->cqe_sz, NULL, flags,
			   mctx->cqe_version == 1, NULL);
}

int32_t mlx5_poll_length_flags_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags)
{
	return poll_length(cq, buf, inl, 0, 64, NULL, flags, 0, NULL);
}

int32_t mlx5_poll_length_flags_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags)
{
	return poll_length(cq, buf, inl, 0, 128, NULL, flags, 0, NULL);
}

int32_t mlx5_poll_length_flags_unsafe_cqe64_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_unsafe_cqe64_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags)
{
	return poll_length(cq, buf, inl, 0, 64, NULL, flags, 1, NULL);
}

int32_t mlx5_poll_length_flags_unsafe_cqe128_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_unsafe_cqe128_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags)
{
	return poll_length(cq, buf, inl, 0, 128, NULL, flags, 1, NULL);
}

/* Poll length flags MP RQ */
int32_t mlx5_poll_length_flags_mp_rq_safe(struct ibv_cq *ibcq, uint32_t *offset, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_safe(struct ibv_cq *ibcq, uint32_t *offset, uint32_t *flags)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);

	return poll_length(ibcq, NULL, NULL, 1, cq->cqe_sz, offset, flags,
			   mctx->cqe_version == 1, NULL);
}

int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe64(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe64(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags)
{
	return poll_length(cq, NULL, NULL, 0, 64, offset, flags, 0, NULL);
}

int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe128(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe128(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags)
{
	return poll_length(cq, NULL, NULL, 0, 128, offset, flags, 0, NULL);
}

int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe64_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe64_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags)
{
	return poll_length(cq, NULL, NULL, 0, 64, offset, flags, 1, NULL);
}

int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe128_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_unsafe_cqe128_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags)
{
	return poll_length(cq, NULL, NULL, 0, 128, offset, flags, 1, NULL);
}

/* Poll length flags cvlan */
int32_t mlx5_poll_length_flags_cvlan_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_cvlan_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);
	return poll_length(ibcq, buf, inl, 1, cq->cqe_sz, NULL, flags,
			   mctx->cqe_version == 1, vlan_cti);
}

int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, buf, inl, 0, 64, NULL, flags, 0, vlan_cti);
}

int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, buf, inl, 0, 128, NULL, flags, 0, vlan_cti);
}

int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe64_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe64_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, buf, inl, 0, 64, NULL, flags, 1, vlan_cti);
}

int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe128_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_cvlan_unsafe_cqe128_v1(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, buf, inl, 0, 128, NULL, flags, 1, vlan_cti);
}

/* Poll length flags MP RQ cvlan */
int32_t mlx5_poll_length_flags_mp_rq_cvlan_safe(struct ibv_cq *ibcq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_cvlan_safe(struct ibv_cq *ibcq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);

	return poll_length(ibcq, NULL, NULL, 1, cq->cqe_sz, offset, flags,
			   mctx->cqe_version == 1, vlan_cti);
}

int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe64(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe64(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, NULL, NULL, 0, 64, offset, flags, 0, vlan_cti);
}

int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe128(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe128(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, NULL, NULL, 0, 128, offset, flags, 0, vlan_cti);
}

int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe64_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe64_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, NULL, NULL, 0, 64, offset, flags, 1, vlan_cti);
}

int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe128_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti) __MLX5_ALGN_F__;
int32_t mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe128_v1(struct ibv_cq *cq, uint32_t *offset, uint32_t *flags, uint16_t *vlan_cti)
{
	return poll_length(cq, NULL, NULL, 0, 128, offset, flags, 1, vlan_cti);
}

static struct ibv_exp_cq_family_v1 mlx5_poll_cq_family_safe = {
	.poll_cnt = mlx5_poll_cnt_safe,
	.poll_length = mlx5_poll_length_safe,
	.poll_length_flags = mlx5_poll_length_flags_safe,
	.poll_length_flags_mp_rq = mlx5_poll_length_flags_mp_rq_safe,
	.poll_length_flags_cvlan = mlx5_poll_length_flags_cvlan_safe,
	.poll_length_flags_mp_rq_cvlan = mlx5_poll_length_flags_mp_rq_cvlan_safe
};

enum mlx5_poll_cq_cqe_sizes {
	MLX5_POLL_CQ_CQE_64		= 1,
	MLX5_POLL_CQ_CQE_128		= 2,
	MLX5_POLL_CQ_NUM_CQE_SIZES	= 3,
};

static struct ibv_exp_cq_family_v1 mlx5_poll_cq_family_unsafe_tbl[MLX5_POLL_CQ_NUM_CQE_SIZES] = {
		[MLX5_POLL_CQ_CQE_64] = {
				.poll_cnt = mlx5_poll_cnt_unsafe_cqe64,
				.poll_length = mlx5_poll_length_unsafe_cqe64,
				.poll_length_flags = mlx5_poll_length_flags_unsafe_cqe64,
				.poll_length_flags_mp_rq = mlx5_poll_length_flags_mp_rq_unsafe_cqe64,
				.poll_length_flags_cvlan = mlx5_poll_length_flags_cvlan_unsafe_cqe64,
				.poll_length_flags_mp_rq_cvlan = mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe64

		},
		[MLX5_POLL_CQ_CQE_128] = {
				.poll_cnt = mlx5_poll_cnt_unsafe_cqe128,
				.poll_length = mlx5_poll_length_unsafe_cqe128,
				.poll_length_flags = mlx5_poll_length_flags_unsafe_cqe128,
				.poll_length_flags_mp_rq = mlx5_poll_length_flags_mp_rq_unsafe_cqe128,
				.poll_length_flags_cvlan = mlx5_poll_length_flags_cvlan_unsafe_cqe128,
				.poll_length_flags_mp_rq_cvlan = mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe128

		},
};

static struct ibv_exp_cq_family_v1 mlx5_poll_cq_family_unsafe_v1_tbl[MLX5_POLL_CQ_NUM_CQE_SIZES] = {
		[MLX5_POLL_CQ_CQE_64] = {
				.poll_cnt = mlx5_poll_cnt_unsafe_cqe64_v1,
				.poll_length = mlx5_poll_length_unsafe_cqe64_v1,
				.poll_length_flags = mlx5_poll_length_flags_unsafe_cqe64_v1,
				.poll_length_flags_mp_rq = mlx5_poll_length_flags_mp_rq_unsafe_cqe64_v1,
				.poll_length_flags_cvlan = mlx5_poll_length_flags_cvlan_unsafe_cqe64_v1,
				.poll_length_flags_mp_rq_cvlan = mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe64_v1
		},
		[MLX5_POLL_CQ_CQE_128] = {
				.poll_cnt = mlx5_poll_cnt_unsafe_cqe128_v1,
				.poll_length = mlx5_poll_length_unsafe_cqe128_v1,
				.poll_length_flags = mlx5_poll_length_flags_unsafe_cqe128_v1,
				.poll_length_flags_mp_rq = mlx5_poll_length_flags_mp_rq_unsafe_cqe128_v1,
				.poll_length_flags_cvlan = mlx5_poll_length_flags_cvlan_unsafe_cqe128_v1,
				.poll_length_flags_mp_rq_cvlan = mlx5_poll_length_flags_mp_rq_cvlan_unsafe_cqe128_v1
		},
};

struct ibv_exp_cq_family_v1 *mlx5_get_poll_cq_family(struct mlx5_cq *cq,
						     struct ibv_exp_query_intf_params *params,
						     enum ibv_exp_query_intf_status *status)
{
	struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);
	enum mlx5_poll_cq_cqe_sizes cqe_size;

	if (params->intf_version > MLX5_MAX_CQ_FAMILY_VER) {
		*status = IBV_EXP_INTF_STAT_VERSION_NOT_SUPPORTED;

		return NULL;
	}
	if (params->flags) {
		fprintf(stderr, PFX "Global interface flags(0x%x) are not supported for CQ family\n", params->flags);
		*status = IBV_EXP_INTF_STAT_FLAGS_NOT_SUPPORTED;

		return NULL;
	}
	if (params->family_flags) {
		fprintf(stderr, PFX "Family flags(0x%x) are not supported for CQ family\n", params->family_flags);
		*status = IBV_EXP_INTF_STAT_FAMILY_FLAGS_NOT_SUPPORTED;

		return NULL;
	}
	if (cq->model_flags & MLX5_CQ_MODEL_FLAG_THREAD_SAFE)
		return &mlx5_poll_cq_family_safe;

	if (cq->cqe_sz == 64) {
		cqe_size = MLX5_POLL_CQ_CQE_64;
	} else if (cq->cqe_sz == 128) {
		cqe_size = MLX5_POLL_CQ_CQE_128;
	} else {
		errno = EINVAL;
		*status = IBV_EXP_INTF_STAT_INVAL_PARARM;
		return NULL;
	}

	if (mctx->cqe_version == 1)
		return &mlx5_poll_cq_family_unsafe_v1_tbl[cqe_size];

	return &mlx5_poll_cq_family_unsafe_tbl[cqe_size];
}
