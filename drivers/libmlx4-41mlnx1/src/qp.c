/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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

#include <stdlib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#include "mlx4.h"
#include "doorbell.h"
#include "wqe.h"

#ifndef htobe64
#include <endian.h>
# if __BYTE_ORDER == __LITTLE_ENDIAN
# define htobe64(x) __bswap_64 (x)
# else
# define htobe64(x) (x)
# endif
#endif

#ifdef MLX4_WQE_FORMAT
	#define SET_BYTE_COUNT(byte_count) (htonl(byte_count) | owner_bit)
	#define WQE_CTRL_OWN	(1 << 30)
#else
	#define SET_BYTE_COUNT(byte_count) htonl(byte_count)
	#define WQE_CTRL_OWN	(1 << 31)
#endif
enum {
	MLX4_OPCODE_BASIC	= 0x00010000,
	MLX4_OPCODE_MANAGED	= 0x00020000,

	MLX4_OPCODE_WITH_IMM	= 0x01000000
};

#define MLX4_IB_OPCODE(op, class, attr)     (((class) & 0x00FF0000) | ((attr) & 0xFF000000) | ((op) & 0x0000FFFF))
#define MLX4_IB_OPCODE_GET_CLASS(opcode)    ((opcode) & 0x00FF0000)
#define MLX4_IB_OPCODE_GET_OP(opcode)       ((opcode) & 0x0000FFFF)
#define MLX4_IB_OPCODE_GET_ATTR(opcode)     ((opcode) & 0xFF000000)

static const uint32_t mlx4_ib_opcode[] = {
	[IBV_WR_SEND]			= MLX4_OPCODE_SEND,
	[IBV_WR_SEND_WITH_IMM]		= MLX4_OPCODE_SEND_IMM,
	[IBV_WR_RDMA_WRITE]		= MLX4_OPCODE_RDMA_WRITE,
	[IBV_WR_RDMA_WRITE_WITH_IMM]	= MLX4_OPCODE_RDMA_WRITE_IMM,
	[IBV_WR_RDMA_READ]		= MLX4_OPCODE_RDMA_READ,
	[IBV_WR_ATOMIC_CMP_AND_SWP]	= MLX4_OPCODE_ATOMIC_CS,
	[IBV_WR_ATOMIC_FETCH_AND_ADD]	= MLX4_OPCODE_ATOMIC_FA,
	[IBV_WR_LOCAL_INV]		= MLX4_OPCODE_LOCAL_INVAL,
	[IBV_WR_BIND_MW]		= MLX4_OPCODE_BIND_MW,
	[IBV_WR_SEND_WITH_INV]		= MLX4_OPCODE_SEND_INVAL,
};


static const uint32_t mlx4_ib_opcode_exp[] = {
	[IBV_EXP_WR_SEND]                   = MLX4_IB_OPCODE(MLX4_OPCODE_SEND,                MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_SEND_WITH_IMM]          = MLX4_IB_OPCODE(MLX4_OPCODE_SEND_IMM,            MLX4_OPCODE_BASIC, MLX4_OPCODE_WITH_IMM),
	[IBV_EXP_WR_RDMA_WRITE]             = MLX4_IB_OPCODE(MLX4_OPCODE_RDMA_WRITE,          MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_RDMA_WRITE_WITH_IMM]    = MLX4_IB_OPCODE(MLX4_OPCODE_RDMA_WRITE_IMM,      MLX4_OPCODE_BASIC, MLX4_OPCODE_WITH_IMM),
	[IBV_EXP_WR_RDMA_READ]              = MLX4_IB_OPCODE(MLX4_OPCODE_RDMA_READ,           MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_ATOMIC_CMP_AND_SWP]     = MLX4_IB_OPCODE(MLX4_OPCODE_ATOMIC_CS,           MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_ATOMIC_FETCH_AND_ADD]   = MLX4_IB_OPCODE(MLX4_OPCODE_ATOMIC_FA,           MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP]   = MLX4_IB_OPCODE(MLX4_OPCODE_ATOMIC_MASK_CS,  MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD] = MLX4_IB_OPCODE(MLX4_OPCODE_ATOMIC_MASK_FA,  MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_LOCAL_INV]              = MLX4_IB_OPCODE(MLX4_OPCODE_LOCAL_INVAL,	      MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_SEND_WITH_INV]          = MLX4_IB_OPCODE(MLX4_OPCODE_SEND_INVAL,          MLX4_OPCODE_BASIC, MLX4_OPCODE_WITH_IMM),
	[IBV_EXP_WR_BIND_MW]                = MLX4_IB_OPCODE(MLX4_OPCODE_BIND_MW,             MLX4_OPCODE_BASIC, 0),
	[IBV_EXP_WR_SEND_ENABLE]            = MLX4_IB_OPCODE(MLX4_OPCODE_SEND_ENABLE,         MLX4_OPCODE_MANAGED, 0),
	[IBV_EXP_WR_RECV_ENABLE]            = MLX4_IB_OPCODE(MLX4_OPCODE_RECV_ENABLE,         MLX4_OPCODE_MANAGED, 0),
	[IBV_EXP_WR_CQE_WAIT]               = MLX4_IB_OPCODE(MLX4_OPCODE_CQE_WAIT,            MLX4_OPCODE_MANAGED, 0),
};

enum {
	MLX4_CALC_FLOAT64_ADD   = 0x00,
	MLX4_CALC_UINT64_ADD    = 0x01,
	MLX4_CALC_UINT64_MAXLOC = 0x02,
	MLX4_CALC_UINT64_AND    = 0x03,
	MLX4_CALC_UINT64_XOR    = 0x04,
	MLX4_CALC_UINT64_OR     = 0x05
};

enum {
	MLX4_WQE_CTRL_CALC_OP = 26
};

static const struct mlx4_calc_op {
	int valid;
	uint32_t opcode;
}  mlx4_calc_ops_table
	[IBV_EXP_CALC_DATA_SIZE_NUMBER]
		[IBV_EXP_CALC_OP_NUMBER]
			[IBV_EXP_CALC_DATA_TYPE_NUMBER] = {
	[IBV_EXP_CALC_DATA_SIZE_64_BIT] = {
		[IBV_EXP_CALC_OP_ADD] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_ADD << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_ADD << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opcode = MLX4_CALC_FLOAT64_ADD << MLX4_WQE_CTRL_CALC_OP }
		},
		[IBV_EXP_CALC_OP_BXOR] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_XOR << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_XOR << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_XOR << MLX4_WQE_CTRL_CALC_OP }
		},
		[IBV_EXP_CALC_OP_BAND] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_AND << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_AND << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_AND << MLX4_WQE_CTRL_CALC_OP }
		},
		[IBV_EXP_CALC_OP_BOR] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_OR << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_OR << MLX4_WQE_CTRL_CALC_OP },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_OR << MLX4_WQE_CTRL_CALC_OP }
		},
		[IBV_EXP_CALC_OP_MAXLOC] = {
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opcode = MLX4_CALC_UINT64_MAXLOC << MLX4_WQE_CTRL_CALC_OP }
		}
	}
};

static int post_send_other(struct ibv_send_wr *wr,
			   struct mlx4_qp *qp,
			   void *wqe_add, int *total_size,
			   int *inl, unsigned int ind) __MLX4_ALGN_FUNC__;
static int post_send_rc_raw_packet(struct ibv_send_wr *wr,
				   struct mlx4_qp *qp,
				   void *wqe_add, int *total_size,
				   int *inl, unsigned int ind) __MLX4_ALGN_FUNC__;
static int post_send_ud(struct ibv_send_wr *wr,
			struct mlx4_qp *qp,
			void *wqe_add, int *total_size,
			int *inl, unsigned int ind) __MLX4_ALGN_FUNC__;
static int post_send_rc_uc(struct ibv_send_wr *wr,
			   struct mlx4_qp *qp,
			   void *wqe_add, int *total_size,
			   int *inl, unsigned int ind) __MLX4_ALGN_FUNC__;
static int post_send_xrc(struct ibv_send_wr *wr,
			 struct mlx4_qp *qp,
			 void *wqe_add, int *total_size,
			 int *inl, unsigned int ind) __MLX4_ALGN_FUNC__;

#define MLX4_WAIT_EN_VALID (1<<30)

static inline void set_wait_en_seg(void *wqe_seg, uint32_t obj_num, uint32_t count) __attribute__((always_inline));
static inline void set_wait_en_seg(void *wqe_seg, uint32_t obj_num, uint32_t count)
{
	struct mlx4_wqe_wait_en_seg *seg = (struct mlx4_wqe_wait_en_seg *)wqe_seg;

	seg->valid   = htonl(MLX4_WAIT_EN_VALID);
	seg->pi      = htonl(count);
	seg->obj_num = htonl(obj_num);

	return;
}

static inline void *get_recv_wqe(struct mlx4_qp *qp, int n) __attribute__((always_inline));
static inline void *get_recv_wqe(struct mlx4_qp *qp, int n)
{
	return qp->rq.buf + (n << qp->rq.wqe_shift);
}

void *mlx4_get_recv_wqe(struct mlx4_qp *qp, int n)
{
	return get_recv_wqe(qp, n);
}

static void *get_send_wqe64(struct mlx4_qp *qp, unsigned int n)
{
	return qp->sq.buf + (n << 6);
}
static void *get_send_wqe(struct mlx4_qp *qp, unsigned int n)
{
	return qp->sq.buf + (n << qp->sq.wqe_shift);
}

/*
 * Stamp a SQ WQE so that it is invalid if prefetched by marking the
 * first four bytes of every 64 byte chunk with 0xffffffff, except for
 * the very first chunk of the WQE.
 */
void mlx4_init_qp_indices(struct mlx4_qp *qp)
{
	qp->sq.head	 = 0;
	qp->sq.tail	 = 0;
	qp->rq.head	 = 0;
	qp->rq.tail	 = 0;
	qp->sq.head_en_index = 0;
	qp->sq.head_en_count = 0;
	qp->rq.head_en_index = 0;
	qp->rq.head_en_count = 0;
}

#ifdef MLX4_WQE_FORMAT
void mlx4_qp_init_sq_ownership(struct mlx4_qp *qp)
{
	__be32 *wqe = get_send_wqe(qp, 0);
	int wq_size = (qp->sq.wqe_cnt << qp->sq.wqe_shift);
	int i;

	for (i = 0; i < wq_size; i += 64)
		wqe[i / 4] = htonl(WQE_CTRL_OWN);
}

static void set_owner_wqe(struct mlx4_qp *qp, unsigned int idx, int ds,
			  uint32_t owner_bit)
{
	uint32_t *wqe;
	int max_sz = (1 << qp->sq.wqe_shift) / 4;
	int cur_sz = ds * 4;
	int tail_sz;
	int i;

	if (max_sz - cur_sz < 16)
		return;

	wqe = get_send_wqe(qp, idx & (qp->sq.wqe_cnt - 1));
	tail_sz = max_sz - cur_sz;
	for (i = 0; tail_sz > 16; i += 4, tail_sz -= 16)
		wqe[cur_sz + i * 4] = owner_bit;
}
#else
static void stamp_send_wqe(struct mlx4_qp *qp, unsigned int n)
{
	uint32_t *wqe = get_send_wqe(qp, n);
	int i;
	int ds = (((struct mlx4_wqe_ctrl_seg *)wqe)->fence_size & 0x3f) << 2;

	for (i = 16; i < ds; i += 16)
		wqe[i] = 0xffffffff;
}

void mlx4_qp_init_sq_ownership(struct mlx4_qp *qp)
{
	struct mlx4_wqe_ctrl_seg *ctrl;
	int i;

	for (i = 0; i < qp->sq.wqe_cnt; ++i) {
		ctrl = get_send_wqe(qp, i);
		ctrl->owner_opcode = htonl(WQE_CTRL_OWN);
		ctrl->fence_size = 1 << (qp->sq.wqe_shift - 4);

		stamp_send_wqe(qp, i);
	}
}
#endif

static int __wq_overflow(struct mlx4_wq *wq, int nreq, struct mlx4_qp *qp) __attribute__((noinline));
static int __wq_overflow(struct mlx4_wq *wq, int nreq, struct mlx4_qp *qp)
{
	struct mlx4_cq *cq = to_mcq(qp->verbs_qp.qp.send_cq);
	unsigned cur;

	mlx4_lock(&cq->lock);
	cur = wq->head - wq->tail;
	mlx4_unlock(&cq->lock);

	return cur + nreq >= wq->max_post;
}

static inline int wq_overflow(struct mlx4_wq *wq, int nreq, struct mlx4_qp *qp) __attribute__((always_inline));
static inline int wq_overflow(struct mlx4_wq *wq, int nreq, struct mlx4_qp *qp)
{
	unsigned cur;

	cur = wq->head - wq->tail;
	if (likely(cur + nreq < wq->max_post))
		return 0;

	return __wq_overflow(wq, nreq, qp);
}

static inline void set_local_inv_seg(struct mlx4_wqe_local_inval_seg *iseg,
		uint32_t rkey) __attribute__((always_inline));
static inline void set_local_inv_seg(struct mlx4_wqe_local_inval_seg *iseg,
		uint32_t rkey)
{
	iseg->mem_key	= htonl(rkey);

	iseg->reserved1    = 0;
	iseg->reserved2    = 0;
	iseg->reserved3[0] = 0;
	iseg->reserved3[1] = 0;
}

static void set_bind_seg(struct mlx4_wqe_bind_seg *bseg, struct ibv_send_wr *wr)
{
	int acc = wr->bind_mw.bind_info.mw_access_flags;
	bseg->flags1 = 0;
	if (acc & IBV_ACCESS_REMOTE_ATOMIC)
		bseg->flags1 |= htonl(MLX4_WQE_MW_ATOMIC);
	if (acc & IBV_ACCESS_REMOTE_WRITE)
		bseg->flags1 |= htonl(MLX4_WQE_MW_REMOTE_WRITE);
	if (acc & IBV_ACCESS_REMOTE_READ)
		bseg->flags1 |= htonl(MLX4_WQE_MW_REMOTE_READ);

	bseg->flags2 = 0;
	if (((struct ibv_mw *)(wr->bind_mw.mw))->type == IBV_MW_TYPE_2)
		bseg->flags2 |= htonl(MLX4_WQE_BIND_TYPE_2);
	if (acc & IBV_ACCESS_ZERO_BASED)
		bseg->flags2 |= htonl(MLX4_WQE_BIND_ZERO_BASED);

	bseg->new_rkey = htonl(wr->bind_mw.rkey);
	bseg->lkey = htonl(wr->bind_mw.bind_info.mr->lkey);
	bseg->addr = htobe64((uint64_t) wr->bind_mw.bind_info.addr);
	bseg->length = htobe64(wr->bind_mw.bind_info.length);
}

static inline void set_raddr_seg(struct mlx4_wqe_raddr_seg *rseg,
				 uint64_t remote_addr, uint32_t rkey) __attribute__((always_inline));
static inline void set_raddr_seg(struct mlx4_wqe_raddr_seg *rseg,
				 uint64_t remote_addr, uint32_t rkey)
{
	rseg->raddr    = htonll(remote_addr);
	rseg->rkey     = htonl(rkey);
	rseg->reserved = 0;
}

static void set_atomic_seg(struct mlx4_wqe_atomic_seg *aseg,
			   struct ibv_exp_send_wr *wr)
{
	struct ibv_exp_fetch_add *fa;

	if (wr->exp_opcode == IBV_EXP_WR_ATOMIC_CMP_AND_SWP) {
		aseg->swap_add = htonll(wr->wr.atomic.swap);
		aseg->compare  = htonll(wr->wr.atomic.compare_add);
	} else if (wr->exp_opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD) {
		fa = &wr->ext_op.masked_atomics.wr_data.inline_data.op.fetch_add;
		aseg->swap_add = htonll(fa->add_val);
		aseg->compare = htonll(fa->field_boundary);
	} else {
		aseg->swap_add = htonll(wr->wr.atomic.compare_add);
		aseg->compare  = 0;
	}
}

static void set_masked_atomic_seg(struct mlx4_wqe_masked_atomic_seg *aseg,
				  struct ibv_exp_send_wr *wr)
{
	struct ibv_exp_cmp_swap *cs = &wr->ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap;

	aseg->swap_data = htonll(cs->swap_val);
	aseg->cmp_data = htonll(cs->compare_val);
	aseg->swap_mask = htonll(cs->swap_mask);
	aseg->cmp_mask = htonll(cs->compare_mask);
}

static void set_datagram_seg(struct mlx4_wqe_datagram_seg *dseg,
			     struct ibv_send_wr *wr)
{
  // Copy address vector
  size_t *dst_av = (size_t *)dseg->av;
  size_t *src_av = (size_t *)&to_mah(wr->wr.ud.ah)->av;
  dst_av[0] = src_av[0];
  dst_av[1] = src_av[1];
  dst_av[2] = src_av[2];
  dst_av[3] = src_av[3];

	dseg->dqpn = htonl(wr->wr.ud.remote_qpn);
	dseg->qkey = htonl(wr->wr.ud.remote_qkey);
	dseg->vlan = htons(to_mah(wr->wr.ud.ah)->vlan);

  uint8_t *dst_mac = dseg->mac;
  uint8_t *src_mac = to_mah(wr->wr.ud.ah)->mac;
  dst_mac[0] = src_mac[0];
  dst_mac[1] = src_mac[1];
  dst_mac[2] = src_mac[2];
  dst_mac[3] = src_mac[3];
  dst_mac[4] = src_mac[4];
  dst_mac[5] = src_mac[5];
}

static  inline void __set_data_seg(struct mlx4_wqe_data_seg *dseg, struct ibv_sge *sg) __attribute__((always_inline));
static  inline void __set_data_seg(struct mlx4_wqe_data_seg *dseg, struct ibv_sge *sg)
{
	dseg->byte_count = htonl(sg->length);
	dseg->lkey       = htonl(sg->lkey);
	dseg->addr       = htonll(sg->addr);
}

static inline void set_ptr_data(struct mlx4_wqe_data_seg *dseg,
				struct ibv_sge *sg, unsigned int owner_bit) __attribute__((always_inline));
static inline void set_ptr_data(struct mlx4_wqe_data_seg *dseg,
				struct ibv_sge *sg, unsigned int owner_bit)
{
	dseg->lkey       = htonl(sg->lkey);
	dseg->addr       = htonll(sg->addr);

	/*
	 * Need a barrier here before writing the byte_count field to
	 * make sure that all the data is visible before the
	 * byte_count field is set.  Otherwise, if the segment begins
	 * a new cacheline, the HCA prefetcher could grab the 64-byte
	 * chunk and get a valid (!= * 0xffffffff) byte count but
	 * stale data, and end up sending the wrong data.
	 */
	wmb();

	if (likely(sg->length))
		dseg->byte_count = SET_BYTE_COUNT(sg->length);
	else
		dseg->byte_count = htonl(0x80000000);
}

/* Convert WQE format to fit BF usage */
static inline void convert_to_bf_wqe(struct mlx4_qp *qp,
				     struct mlx4_wqe_ctrl_seg *ctrl,
				     const unsigned wqe_idx) __attribute__((always_inline));
static inline void convert_to_bf_wqe(struct mlx4_qp *qp,
				     struct mlx4_wqe_ctrl_seg *ctrl,
				     const unsigned wqe_idx)
{
	uint32_t *tmp = (uint32_t *)ctrl->reserved;

	ctrl->owner_opcode |= htonl((wqe_idx & 0xffff) << 8);
	*tmp |= qp->doorbell_qpn;
}

static inline void copy_wqe_to_bf(struct mlx4_qp *qp,
				  struct mlx4_wqe_ctrl_seg *ctrl,
				  const int aligned_size,
				  const unsigned wqe_idx,
				  const int dedic_bf,
				  const int one_thread_auto_evict) __attribute__((always_inline));
static inline void copy_wqe_to_bf(struct mlx4_qp *qp,
				  struct mlx4_wqe_ctrl_seg *ctrl,
				  const int aligned_size,
				  const unsigned wqe_idx,
				  const int dedic_bf,
				  const int one_thread_auto_evict)
{
	convert_to_bf_wqe(qp, ctrl, wqe_idx);

	if (dedic_bf && one_thread_auto_evict)
		/*
		 * In case QP has dedicated BF, only one thread using this QP
		 * and the CPU arch supports auto eviction of WC buffer we can move
		 * the wc_wmb before the bf_copy (usually it is located after the bf_copy).
		 * This provides significant improvement in message rate of small messages.
		 * This barrier keeps BF toggling order by ensuring that previous BF data
		 * is written to memory before writing to the next BF buffer.
		 */
		wc_wmb();
	else
		/*
		 * Make sure that descriptor is written to memory
		 * before writing to BlueFlame page.
		 */
		wmb();

	if (dedic_bf) {
		mlx4_bf_copy(qp->bf->dedic.address, (uint64_t *) ctrl, aligned_size);
	} else {
		mlx4_lock(&qp->bf->cmn.lock);
		mlx4_bf_copy(qp->bf->cmn.address, (uint64_t *) ctrl, aligned_size);
	}
	if (!(dedic_bf && one_thread_auto_evict))
		/*
		 * This barrier ensures that BF data is written to memory
		 * before toggling the BF buffer. This is to keep the right
		 * toggling order and to prevent the case in which next BF data
		 * will be written before the current BF data.
		 * In addition this barrier ensures the eviction of the WC buffer.
		 * See comment above for the conditions in which this barrier may be
		 * set before the bf_copy.
		 */
		wc_wmb();

	if (dedic_bf) {
		/* Toggle BF buffer */
		qp->bf->dedic.address = (void *)((uintptr_t)qp->bf->dedic.address ^ qp->bf_buf_size);
	} else {
		/* Toggle BF buffer */
		qp->bf->cmn.address = (void *)((uintptr_t)qp->bf->cmn.address ^ qp->bf_buf_size);
		mlx4_unlock(&qp->bf->cmn.lock);
	}
}

static inline void __ring_db(struct mlx4_qp *qp, struct mlx4_wqe_ctrl_seg *ctrl,
			     int nreq, int size, int inl,
			     const int use_bf, const int dedic_bf, const int one_thread_auto_evict,
			     const int prefer_bf) __attribute__((always_inline));
static inline void __ring_db(struct mlx4_qp *qp, struct mlx4_wqe_ctrl_seg *ctrl,
			     int nreq, int size, int inl,
			     const int use_bf, const int dedic_bf, const int one_thread_auto_evict,
			     const int prefer_bf)
{
	if (use_bf && nreq == 1 && (inl || prefer_bf) &&
	    size > 1 && size <= qp->bf_buf_size / 16) {
		copy_wqe_to_bf(qp, ctrl, align(size * 16, 64),
			       qp->sq.head , dedic_bf,
			       one_thread_auto_evict);
		++qp->sq.head;
	} else if (likely(nreq)) {
		qp->sq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * ringing non-cached doorbell record.
		 */
		nc_wmb();
		mmio_writel((unsigned long)(qp->sdb), qp->doorbell_qpn);
	}
}

static void __ring_db_mng(struct mlx4_qp *qp, struct mlx4_wqe_ctrl_seg *ctrl,
			  int nreq, int size, int inl) __attribute__((noinline));
static void __ring_db_mng(struct mlx4_qp *qp, struct mlx4_wqe_ctrl_seg *ctrl,
			  int nreq, int size, int inl)
{
	struct mlx4_context *ctx = to_mctx(qp->verbs_qp.qp.context);

	if (nreq == 1 && (inl || ctx->prefer_bf) && size > 1 && size <= qp->bf_buf_size / 16) {
		convert_to_bf_wqe(qp, ctrl, qp->sq.head);

		/*
		 * Make sure that descriptor is written to memory
		 * before writing to BlueFlame page.
		 */
		wmb();

		++qp->sq.head;

		wmb();

	} else if (likely(nreq)) {
		qp->sq.head += nreq;

		/* Controlled qp */
		wmb();
	}
}

static inline void ring_db(struct mlx4_qp *qp, struct mlx4_wqe_ctrl_seg *ctrl,
			   int nreq, int size, int inl) __attribute__((always_inline));
static inline void ring_db(struct mlx4_qp *qp, struct mlx4_wqe_ctrl_seg *ctrl,
			   int nreq, int size, int inl)
{
	if (unlikely(qp->create_flags & IBV_EXP_QP_CREATE_MANAGED_SEND))
		return __ring_db_mng(qp, ctrl, nreq, size, inl);

	switch (qp->db_method) {
	case MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_PB:
		return __ring_db(qp, ctrl, nreq, size, inl, 1, 1, 1, 1);
	case MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_NPB:
		return __ring_db(qp, ctrl, nreq, size, inl, 1, 1, 1, 0);
	case MLX4_QP_DB_METHOD_DEDIC_BF:
		return __ring_db(qp, ctrl, nreq, size, inl, 1, 1, 0, to_mctx(qp->verbs_qp.qp.context)->prefer_bf);
	case MLX4_QP_DB_METHOD_BF:
		return __ring_db(qp, ctrl, nreq, size, inl, 1, 0, 0, to_mctx(qp->verbs_qp.qp.context)->prefer_bf);
	case MLX4_QP_DB_METHOD_DB:
		return __ring_db(qp, ctrl, nreq, size, inl, 0, 0, 0, to_mctx(qp->verbs_qp.qp.context)->prefer_bf);
	}
}

static void set_ctrl_seg(struct mlx4_wqe_ctrl_seg *ctrl, struct ibv_send_wr *wr,
			 struct mlx4_qp *qp, uint32_t imm, uint32_t srcrb_flags,
			 unsigned int owner_bit, int size, uint32_t wr_op)
{
	ctrl->srcrb_flags = srcrb_flags;
	ctrl->imm = imm;
	ctrl->fence_size = (wr->send_flags & IBV_SEND_FENCE ?
			    MLX4_WQE_CTRL_FENCE : 0) | size;

	/*
	 * Make sure descriptor is fully written before
	 * setting ownership bit (because HW can start
	 * executing as soon as we do).
	 */
	wmb();
	ctrl->owner_opcode = htonl(wr_op) | owner_bit;
}

//
// Modifications:
// 1. Works only for num_sge = 1 (it cannot be 0!)
// 2. The data in the SGE must fit in max_inline_data. No error is reported if
//    this is violated.
//
static inline int set_data_inl_seg(struct mlx4_qp *qp, int num_sge, struct ibv_sge *sg_list,
				   void *wqe, int *size, unsigned int owner_bit) __attribute__((always_inline));
static inline int set_data_inl_seg(struct mlx4_qp *qp, int num_sge, struct ibv_sge *sg_list,
				   void *wqe, int *size, unsigned int owner_bit)
{
	struct mlx4_wqe_inline_seg *seg;
	void *addr;
	int len;
	int inl = 0;

	seg = wqe;
	wqe += sizeof(*seg);

  addr = (void *) (uintptr_t) sg_list[0].addr;
  len  = sg_list[0].length;
  inl += len;

  memcpy(wqe, addr, len);
  wqe += len;

  /*
   * Need a barrier here to make sure
   * all the data is visible before the
   * byte_count field is set.  Otherwise
   * the HCA prefetcher could grab the
   * 64-byte chunk with this inline
   * segment and get a valid (!=
   * 0xffffffff) byte count but stale
   * data, and end up sending the wrong
   * data.
   */
  wmb();
  seg->byte_count = SET_BYTE_COUNT((MLX4_INLINE_SEG | len));

	*size += (inl + 1 * sizeof(*seg) + 15) / 16;

	return 0;
}

static inline void set_data_inl_seg_fast(struct mlx4_qp *qp,
					 void *addr, int length,
					 void *wqe, int *size,
					 unsigned int owner_bit) __attribute__((always_inline));
static inline void set_data_inl_seg_fast(struct mlx4_qp *qp,
					 void *addr, int length,
					 void *wqe, int *size,
					 unsigned int owner_bit)
{
	struct mlx4_wqe_inline_seg *seg;
	static const int first_seg_data_size = MLX4_INLINE_ALIGN - sizeof(*seg) - sizeof(struct mlx4_wqe_ctrl_seg);
	static const int seg_data_size = MLX4_INLINE_ALIGN - sizeof(*seg);

	seg = wqe;
	wqe += sizeof(*seg);

	if (length <= first_seg_data_size) {
		/* For the first segment there is no need to make sure
		 * all the data is visible before the byte_count field is set.
		 * This is because the ctrl segment at the beginning of the
		 * segment covers HCA prefetcher issue.
		 */
		seg->byte_count = SET_BYTE_COUNT((MLX4_INLINE_SEG | length));

		memcpy(wqe, addr, length);
		*size += (length + sizeof(*seg) + 15) / 16;
	} else {
		void *start_wqe = seg;

		seg->byte_count = SET_BYTE_COUNT((MLX4_INLINE_SEG | first_seg_data_size));
		memcpy(wqe, addr, first_seg_data_size);
		length -= first_seg_data_size;
		addr += first_seg_data_size;
		seg = (struct mlx4_wqe_inline_seg *)((char *)seg + MLX4_INLINE_ALIGN - sizeof(struct mlx4_wqe_ctrl_seg));
		wqe += MLX4_INLINE_ALIGN - sizeof(struct mlx4_wqe_ctrl_seg);

		while (length > seg_data_size) {
			memcpy(wqe, addr, seg_data_size);
			wmb(); /* see comment below */
			seg->byte_count = SET_BYTE_COUNT((MLX4_INLINE_SEG | seg_data_size));
			length -= seg_data_size ;
			addr += seg_data_size;
			seg = (struct mlx4_wqe_inline_seg *)((char *)seg + MLX4_INLINE_ALIGN);
			wqe += MLX4_INLINE_ALIGN;
		}
		memcpy(wqe, addr, length);

		/*
		 * Need a barrier here to make sure
		 * all the data is visible before the
		 * byte_count field is set.  Otherwise
		 * the HCA prefetcher could grab the
		 * 64-byte chunk with this inline
		 * segment and get a valid (!=
		 * 0xffffffff) byte count but stale
		 * data, and end up sending the wrong
		 * data.
		 */
		wmb();
		seg->byte_count = SET_BYTE_COUNT((MLX4_INLINE_SEG | length));
		*size += (wqe + length - start_wqe + 15) / 16;
	}
}

static inline void set_data_non_inl_seg(struct mlx4_qp *qp, int num_sge, struct ibv_sge *sg_list,
					void *wqe, int *size, unsigned int owner_bit) __attribute__((always_inline));
static inline void set_data_non_inl_seg(struct mlx4_qp *qp, int num_sge, struct ibv_sge *sg_list,
					void *wqe, int *size, unsigned int owner_bit)
{
	if (likely(num_sge == 1)) {
		struct mlx4_wqe_data_seg *seg = wqe;

		set_ptr_data(seg, sg_list, owner_bit);

		*size += (sizeof(*seg) / 16);
	} else {
		struct mlx4_wqe_data_seg *seg = wqe;
		int i;

		for (i = num_sge - 1; i >= 0 ; --i)
			set_ptr_data(seg + i, sg_list + i, owner_bit);

		*size += num_sge * (sizeof(*seg) / 16);
	}
}

static inline int set_data_seg(struct mlx4_qp *qp, void *seg, int *sz, int is_inl,
			       int num_sge, struct ibv_sge *sg_list, int *inl,
			       unsigned int owner_bit) __attribute__((always_inline));
static inline int set_data_seg(struct mlx4_qp *qp, void *seg, int *sz, int is_inl,
			       int num_sge, struct ibv_sge *sg_list, int *inl,
			       unsigned int owner_bit)
{
	if (is_inl) {
		/* inl is set to true if this is an inline data segment and num_sge > 0 */
		*inl = num_sge > 0;
		return set_data_inl_seg(qp, num_sge, sg_list, seg, sz,
					owner_bit);
	}
	set_data_non_inl_seg(qp, num_sge, sg_list, seg, sz, owner_bit);

	return 0;
}

static inline int set_common_segments(struct ibv_send_wr *wr, struct mlx4_qp *qp,
				      uint32_t srcrb_flags, uint32_t imm,
				      void *wqe, void *ctrl, int size, int *total_size,
				      int *inl, unsigned int ind) __attribute__((always_inline));
static inline int set_common_segments(struct ibv_send_wr *wr, struct mlx4_qp *qp,
				      uint32_t srcrb_flags, uint32_t imm,
				      void *wqe, void *ctrl, int size, int *total_size,
				      int *inl, unsigned int ind)
{
	int ret;
	unsigned int owner_bit = (ind & qp->sq.wqe_cnt) ? htonl(WQE_CTRL_OWN) : 0;

	ret = set_data_seg(qp, wqe, &size, !!(wr->send_flags & IBV_SEND_INLINE),
			   wr->num_sge, wr->sg_list, inl, owner_bit);
	if (unlikely(ret))
		return ret;

	*total_size = size;
	set_ctrl_seg(ctrl, wr, qp, imm, srcrb_flags, owner_bit, size,
		     mlx4_ib_opcode[wr->opcode]);

	return 0;

}

static int post_send_other(struct ibv_send_wr *wr,
			   struct mlx4_qp *qp,
			   void *wqe_add, int *total_size,
			   int *inl, unsigned int ind)
{
	void *ctrl = wqe_add;
	void *wqe = wqe_add + sizeof(struct mlx4_wqe_ctrl_seg);
	int size = sizeof(struct mlx4_wqe_ctrl_seg) / 16;
	int idx = (wr->send_flags & IBV_SEND_SIGNALED)/IBV_SEND_SIGNALED |
		  (wr->send_flags & IBV_SEND_SOLICITED)/(IBV_SEND_SOLICITED >> 1);
	uint32_t srcrb_flags = htonl((uint32_t)qp->srcrb_flags_tbl[idx]);
	uint32_t imm = (wr->opcode == IBV_WR_SEND_WITH_IMM ||
			wr->opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
		       ? wr->imm_data : 0;

	return set_common_segments(wr, qp, srcrb_flags, imm, wqe, ctrl, size, total_size, inl, ind);

}

static int post_send_rc_raw_packet(struct ibv_send_wr *wr,
				   struct mlx4_qp *qp,
				   void *wqe_add, int *total_size,
				   int *inl, unsigned int ind)
{
	void *ctrl = wqe_add;
	void *wqe = wqe_add + sizeof(struct mlx4_wqe_ctrl_seg);
	union {
		uint32_t srcrb_flags;
		uint16_t srcrb_flags16[2];
	} u;
	uint32_t imm;
	int idx;
	int size = sizeof(struct mlx4_wqe_ctrl_seg) / 16;

	/* Sanity check - prevent from posting empty SR */
	if (unlikely(!wr->num_sge))
		return EINVAL;

	if (qp->link_layer == IBV_LINK_LAYER_ETHERNET) {
		/* For raw eth, the MLX4_WQE_CTRL_SOLICIT flag is used
		* to indicate that no icrc should be calculated */
		idx = (wr->send_flags & IBV_SEND_SIGNALED)/IBV_SEND_SIGNALED;
		u.srcrb_flags = htonl((uint32_t)(qp->srcrb_flags_tbl[idx] | MLX4_WQE_CTRL_SOLICIT));
		/* For raw eth, take the dmac from the payload */
		u.srcrb_flags16[0] = *(uint16_t *)(uintptr_t)wr->sg_list[0].addr;
		imm = *(uint32_t *)((uintptr_t)(wr->sg_list[0].addr)+2);
	} else {
		idx = (wr->send_flags & IBV_SEND_SIGNALED)/IBV_SEND_SIGNALED |
		      (wr->send_flags & IBV_SEND_SOLICITED)/(IBV_SEND_SOLICITED >> 1);
		u.srcrb_flags = htonl((uint32_t)qp->srcrb_flags_tbl[idx]);

		imm = (wr->opcode == IBV_WR_SEND_WITH_IMM ||
		       wr->opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
		      ? wr->imm_data : 0;
	}

	return set_common_segments(wr, qp, u.srcrb_flags, imm, wqe, ctrl, size, total_size, inl, ind);
}

static int post_send_ud(struct ibv_send_wr *wr,
			struct mlx4_qp *qp,
			void *wqe_add, int *total_size,
			int *inl, unsigned int ind)
{
	void *ctrl = wqe_add;
	void *wqe = wqe_add + sizeof(struct mlx4_wqe_ctrl_seg);
	int size = sizeof(struct mlx4_wqe_ctrl_seg) / 16;
	int idx = (wr->send_flags & IBV_SEND_SIGNALED)/IBV_SEND_SIGNALED |
		  (wr->send_flags & IBV_SEND_SOLICITED)/(IBV_SEND_SOLICITED >> 1);
	uint32_t srcrb_flags = htonl((uint32_t)qp->srcrb_flags_tbl[idx]);
	uint32_t imm = wr->imm_data;

	set_datagram_seg(wqe, wr);
	wqe  += sizeof(struct mlx4_wqe_datagram_seg);
	size += sizeof(struct mlx4_wqe_datagram_seg) / 16;

	return set_common_segments(wr, qp, srcrb_flags, imm, wqe, ctrl, size, total_size, inl, ind);
}

static inline int post_send_connected(struct ibv_send_wr *wr,
				      struct mlx4_qp *qp,
				      void *wqe_add, int *total_size,
				      int *inl, unsigned int ind, int is_xrc) __attribute__((always_inline));
static inline int post_send_connected(struct ibv_send_wr *wr,
				      struct mlx4_qp *qp,
				      void *wqe_add, int *total_size,
				      int *inl, unsigned int ind, int is_xrc)
{
	void *ctrl = wqe_add;
	void *wqe = wqe_add + sizeof(struct mlx4_wqe_ctrl_seg);
	uint32_t srcrb_flags;
	uint32_t imm = 0;
	int size = sizeof(struct mlx4_wqe_ctrl_seg) / 16;
	int idx = (wr->send_flags & IBV_SEND_SIGNALED)/IBV_SEND_SIGNALED |
		  (wr->send_flags & IBV_SEND_SOLICITED)/(IBV_SEND_SOLICITED >> 1);

	if (is_xrc)
		srcrb_flags = htonl((wr->qp_type.xrc.remote_srqn << 8) |
				    (qp->srcrb_flags_tbl[idx]));
	else
		srcrb_flags = htonl((uint32_t)qp->srcrb_flags_tbl[idx]);

	switch (wr->opcode) {
	case IBV_WR_ATOMIC_CMP_AND_SWP:
	case IBV_WR_ATOMIC_FETCH_AND_ADD:
		set_raddr_seg(wqe, wr->wr.atomic.remote_addr,
			      wr->wr.atomic.rkey);
		wqe  += sizeof(struct mlx4_wqe_raddr_seg);

		set_atomic_seg(wqe, (struct ibv_exp_send_wr *)wr);
		wqe  += sizeof(struct mlx4_wqe_atomic_seg);
		size += (sizeof(struct mlx4_wqe_raddr_seg) +
			 sizeof(struct mlx4_wqe_atomic_seg)) / 16;

		break;

	case IBV_WR_SEND_WITH_IMM:
		imm = wr->imm_data;
		break;

	case IBV_WR_RDMA_WRITE_WITH_IMM:
		imm = wr->imm_data;
		if (!wr->num_sge)
			*inl = 1;
		set_raddr_seg(wqe, wr->wr.rdma.remote_addr,
					wr->wr.rdma.rkey);
		wqe  += sizeof(struct mlx4_wqe_raddr_seg);
		size += sizeof(struct mlx4_wqe_raddr_seg) / 16;
		break;

	case IBV_WR_RDMA_READ:
		*inl = 1;
		/* fall through */
	case IBV_WR_RDMA_WRITE:
		set_raddr_seg(wqe, wr->wr.rdma.remote_addr,
					wr->wr.rdma.rkey);
		wqe  += sizeof(struct mlx4_wqe_raddr_seg);
		size += sizeof(struct mlx4_wqe_raddr_seg) / 16;

		break;

	case IBV_WR_SEND:
		break;

	default:
		/* No extra segments required for sends */
		break;
	}

	return set_common_segments(wr, qp, srcrb_flags, imm, wqe, ctrl, size, total_size, inl, ind);
}

static int post_send_rc_uc(struct ibv_send_wr *wr,
			   struct mlx4_qp *qp,
			   void *wqe_add, int *total_size,
			   int *inl, unsigned int ind)
{
	return post_send_connected(wr, qp, wqe_add, total_size, inl, ind, 0);
}

static int post_send_xrc(struct ibv_send_wr *wr,
			 struct mlx4_qp *qp,
			 void *wqe_add, int *total_size,
			 int *inl, unsigned int ind)
{
	return post_send_connected(wr, qp, wqe_add, total_size, inl, ind, 1);
}

void mlx4_update_post_send_one(struct mlx4_qp *qp)
{
	switch (qp->qp_type) {
	case IBV_QPT_XRC_SEND:
	case IBV_QPT_XRC:
		qp->post_send_one = post_send_xrc;
		break;
	case IBV_QPT_RC:
	case IBV_QPT_UC:
		qp->post_send_one = post_send_rc_uc;
		break;
	case IBV_QPT_UD:
		qp->post_send_one = post_send_ud;
		break;

	case IBV_QPT_RAW_PACKET:
		qp->post_send_one = post_send_rc_raw_packet;
		break;

	default:
		qp->post_send_one = post_send_other;
		break;
	}
}

//
// Changes:
// 1. Directly call post_send_ud() instead of using function pointer
// 2. Ignore WQ overflow check
// 3. Ignore num_sge > max_gs check
// 4. Ignore invalid opcode check
// 5. Don't grab QP lock (this lock is elided anyway with MLX4_SINGLE_THREADED)
//
int mlx4_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
		     struct ibv_send_wr **bad_wr)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	void *uninitialized_var(ctrl);
	unsigned int ind;
	int nreq;
	int inl = 0;
	int ret = 0;
	int size = 0;

	ind = qp->sq.head;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		ctrl = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
		qp->sq.wrid[ind & (qp->sq.wqe_cnt - 1)] = wr->wr_id;

		ret = post_send_ud(wr, qp, ctrl, &size, &inl, ind);
		if (unlikely(ret)) {
			inl = 0;
			errno = ret;
			*bad_wr = wr;
			goto out;
		}
		/*
		 * We can improve latency by not stamping the last
		 * send queue WQE until after ringing the doorbell, so
		 * only stamp here if there are still more WQEs to post.
		 */
		if (likely(wr->next))
#ifndef MLX4_WQE_FORMAT
			stamp_send_wqe(qp, (ind + qp->sq_spare_wqes) &
				       (qp->sq.wqe_cnt - 1));
#else
			/* Make sure all owners bits are set to HW ownership */
			set_owner_wqe(qp, ind, size,
				      ((ind & qp->sq.wqe_cnt) ? htonl(WQE_CTRL_OWN) : 0));
#endif

		++ind;
	}

out:
	ring_db(qp, ctrl, nreq, size, inl);

	if (likely(nreq))
#ifndef MLX4_WQE_FORMAT
		stamp_send_wqe(qp, (ind + qp->sq_spare_wqes - 1) &
			       (qp->sq.wqe_cnt - 1));
#else
		set_owner_wqe(qp, ind - 1, size,
			      ((ind - 1) & qp->sq.wqe_cnt ? htonl(WQE_CTRL_OWN) : 0));
#endif

	return ret;
}

int mlx4_exp_post_send(struct ibv_qp *ibqp, struct ibv_exp_send_wr *wr,
		     struct ibv_exp_send_wr **bad_wr)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	void *wqe;
	void *uninitialized_var(ctrl);
	union {
		uint32_t srcrb_flags;
		uint16_t srcrb_flags16[2];
	} u;
	uint32_t imm;
	int idx;
	unsigned int ind;
	int uninitialized_var(owner_bit);
	int nreq;
	int inl = 0;
	int ret = 0;
	int size = 0;
	uint32_t mlx4_wr_op;
	uint64_t exp_send_flags;

	mlx4_lock(&qp->sq.lock);

	/* XXX check that state is OK to post send */

	ind = qp->sq.head;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		exp_send_flags = wr->exp_send_flags;

		if (unlikely(!(qp->create_flags & IBV_EXP_QP_CREATE_IGNORE_SQ_OVERFLOW) &&
			wq_overflow(&qp->sq, nreq, qp))) {
			ret = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->sq.max_gs)) {
			ret = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->exp_opcode >= sizeof(mlx4_ib_opcode_exp) / sizeof(mlx4_ib_opcode_exp[0]))) {
			ret = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		if (((MLX4_IB_OPCODE_GET_CLASS(mlx4_ib_opcode_exp[wr->exp_opcode]) == MLX4_OPCODE_MANAGED) ||
		      (exp_send_flags & IBV_EXP_SEND_WITH_CALC)) &&
		     !(qp->create_flags & IBV_EXP_QP_CREATE_CROSS_CHANNEL)) {
			ret = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		mlx4_wr_op = MLX4_IB_OPCODE_GET_OP(mlx4_ib_opcode_exp[wr->exp_opcode]);

		ctrl = wqe = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
		qp->sq.wrid[ind & (qp->sq.wqe_cnt - 1)] = wr->wr_id;
		owner_bit = ind & qp->sq.wqe_cnt ? htonl(WQE_CTRL_OWN) : 0;

		idx = (exp_send_flags & IBV_EXP_SEND_SIGNALED)/IBV_EXP_SEND_SIGNALED |
		      (exp_send_flags & IBV_EXP_SEND_SOLICITED)/(IBV_EXP_SEND_SOLICITED >> 1) |
		      (exp_send_flags & IBV_EXP_SEND_IP_CSUM)/(IBV_EXP_SEND_IP_CSUM >> 2);
		u.srcrb_flags = htonl((uint32_t)qp->srcrb_flags_tbl[idx]);

		imm = (MLX4_IB_OPCODE_GET_ATTR(mlx4_ib_opcode_exp[wr->exp_opcode]) & MLX4_OPCODE_WITH_IMM ?
		      wr->ex.imm_data : 0);

		wqe += sizeof(struct mlx4_wqe_ctrl_seg);
		size = sizeof(struct mlx4_wqe_ctrl_seg) / 16;

		switch (qp->qp_type) {
		case IBV_QPT_XRC_SEND:
		case IBV_QPT_XRC:
			u.srcrb_flags |= MLX4_REMOTE_SRQN_FLAGS(wr);
			/* fall through */
		case IBV_QPT_RC:
		case IBV_QPT_UC:
			switch (wr->exp_opcode) {
			case IBV_EXP_WR_ATOMIC_CMP_AND_SWP:
			case IBV_EXP_WR_ATOMIC_FETCH_AND_ADD:
			case IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD:
				if (wr->exp_opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD) {
					if (!qp->is_masked_atomic) {
						ret = EINVAL;
						*bad_wr = wr;
						goto out;
					}
					set_raddr_seg(wqe,
						      wr->ext_op.masked_atomics.remote_addr,
						      wr->ext_op.masked_atomics.rkey);
				} else {
					set_raddr_seg(wqe, wr->wr.atomic.remote_addr,
						      wr->wr.atomic.rkey);
				}
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);

				set_atomic_seg(wqe, wr);
				wqe  += sizeof (struct mlx4_wqe_atomic_seg);
				size += (sizeof (struct mlx4_wqe_raddr_seg) +
					 sizeof (struct mlx4_wqe_atomic_seg)) / 16;

				break;

			case IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP:
				if (!qp->is_masked_atomic) {
					ret = EINVAL;
					*bad_wr = wr;
					goto out;
				}
				set_raddr_seg(wqe,
					      wr->ext_op.masked_atomics.remote_addr,
					      wr->ext_op.masked_atomics.rkey);
				wqe += sizeof(struct mlx4_wqe_raddr_seg);

				set_masked_atomic_seg(wqe, wr);
				wqe  += sizeof(struct mlx4_wqe_masked_atomic_seg);
				size += (sizeof(struct mlx4_wqe_raddr_seg) +
					 sizeof(struct mlx4_wqe_masked_atomic_seg)) / 16;
				break;

			case IBV_EXP_WR_RDMA_READ:
				inl = 1;
				/* fall through */
			case IBV_EXP_WR_RDMA_WRITE_WITH_IMM:
				if (!wr->num_sge)
					inl = 1;
				/* fall through */
			case IBV_EXP_WR_RDMA_WRITE:
				if (exp_send_flags & IBV_EXP_SEND_WITH_CALC) {

					if ((uint32_t)wr->op.calc.data_size >= IBV_EXP_CALC_DATA_SIZE_NUMBER ||
					    (uint32_t)wr->op.calc.calc_op >= IBV_EXP_CALC_OP_NUMBER ||
					    (uint32_t)wr->op.calc.data_type >= IBV_EXP_CALC_DATA_TYPE_NUMBER ||
					    !mlx4_calc_ops_table
						[wr->op.calc.data_size]
							[wr->op.calc.calc_op]
								[wr->op.calc.data_type].valid) {
						ret = -1;
						*bad_wr = wr;
						goto out;
					}

					mlx4_wr_op = MLX4_OPCODE_CALC_RDMA_WRITE_IMM |
							mlx4_calc_ops_table
								[wr->op.calc.data_size]
									[wr->op.calc.calc_op]
										[wr->op.calc.data_type].opcode;
					set_raddr_seg(wqe, wr->wr.rdma.remote_addr,
								wr->wr.rdma.rkey);

				} else {
					set_raddr_seg(wqe, wr->wr.rdma.remote_addr,
							wr->wr.rdma.rkey);
				}
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);
				size += sizeof (struct mlx4_wqe_raddr_seg) / 16;

				break;

			case IBV_EXP_WR_SEND:
				if (exp_send_flags & IBV_EXP_SEND_WITH_CALC) {

					if ((uint32_t)wr->op.calc.data_size >= IBV_EXP_CALC_DATA_SIZE_NUMBER ||
					    (uint32_t)wr->op.calc.calc_op >= IBV_EXP_CALC_OP_NUMBER ||
					    (uint32_t)wr->op.calc.data_type >= IBV_EXP_CALC_DATA_TYPE_NUMBER ||
					    !mlx4_calc_ops_table
						[wr->op.calc.data_size]
							[wr->op.calc.calc_op]
								[wr->op.calc.data_type].valid) {
						ret = -1;
						*bad_wr = wr;
						goto out;
					}

					mlx4_wr_op = MLX4_OPCODE_CALC_SEND |
							mlx4_calc_ops_table
								[wr->op.calc.data_size]
									[wr->op.calc.calc_op]
										[wr->op.calc.data_type].opcode;
				}

				break;

			case IBV_EXP_WR_CQE_WAIT:
				{
					struct mlx4_cq *wait_cq = to_mcq(wr->task.cqe_wait.cq);
					uint32_t wait_index = 0;

					wait_index = wait_cq->wait_index +
								wr->task.cqe_wait.cq_count;
					wait_cq->wait_count = max(wait_cq->wait_count,
								wr->task.cqe_wait.cq_count);

					if (exp_send_flags & IBV_EXP_SEND_WAIT_EN_LAST) {
						wait_cq->wait_index += wait_cq->wait_count;
						wait_cq->wait_count = 0;
					}

					set_wait_en_seg(wqe, wait_cq->cqn, wait_index);
					wqe   += sizeof(struct mlx4_wqe_wait_en_seg);
					size += sizeof(struct mlx4_wqe_wait_en_seg) / 16;
				}
				break;

			case IBV_EXP_WR_SEND_ENABLE:
			case IBV_EXP_WR_RECV_ENABLE:
				{
					unsigned head_en_index;
					struct mlx4_wq *wq;

					/*
					 * Posting work request for QP that does not support
					 * SEND/RECV ENABLE makes performance worse.
					 */
					if (((wr->exp_opcode == IBV_EXP_WR_SEND_ENABLE) &&
					     !(to_mqp(wr->task.wqe_enable.qp)->create_flags &
							     IBV_EXP_QP_CREATE_MANAGED_SEND)) ||
					     ((wr->exp_opcode == IBV_EXP_WR_RECV_ENABLE) &&
					     !(to_mqp(wr->task.wqe_enable.qp)->create_flags &
							     IBV_EXP_QP_CREATE_MANAGED_RECV))) {
						ret = -1;
						*bad_wr = wr;
						goto out;
					}

					wq = (wr->exp_opcode == IBV_EXP_WR_SEND_ENABLE) ?
						&to_mqp(wr->task.wqe_enable.qp)->sq :
						&to_mqp(wr->task.wqe_enable.qp)->rq;

					/* If wqe_count is 0 release all WRs from queue */
					if (wr->task.wqe_enable.wqe_count) {
						head_en_index = wq->head_en_index +
								wr->task.wqe_enable.wqe_count;
						wq->head_en_count = max(wq->head_en_count,
								wr->task.wqe_enable.wqe_count);

						if ((int)(wq->head - head_en_index) < 0) {
							ret = -1;
							*bad_wr = wr;
							goto out;
						}
					} else {
						head_en_index = wq->head;
						wq->head_en_count = wq->head - wq->head_en_index;
					}

					if (exp_send_flags & IBV_EXP_SEND_WAIT_EN_LAST) {
						wq->head_en_index += wq->head_en_count;
						wq->head_en_count = 0;
					}

					set_wait_en_seg(wqe,
							wr->task.wqe_enable.qp->qp_num,
							head_en_index);

					wqe += sizeof(struct mlx4_wqe_wait_en_seg);
					size += sizeof(struct mlx4_wqe_wait_en_seg) / 16;
				}
				break;

			case IBV_EXP_WR_SEND_WITH_INV:
				imm = htonl(wr->ex.invalidate_rkey);
				break;

			default:
				/* No extra segments required for sends */
				break;
			}
			break;

		case IBV_QPT_UD:
			set_datagram_seg(wqe, (struct ibv_send_wr *)wr);
			wqe  += sizeof (struct mlx4_wqe_datagram_seg);
			size += sizeof (struct mlx4_wqe_datagram_seg) / 16;
			break;

		case IBV_QPT_RAW_PACKET:
			/* Sanity check - prevent from posting empty SR */
			if (unlikely(!wr->num_sge)) {
				ret = EINVAL;
				*bad_wr = wr;
				goto out;
			}
			if (qp->link_layer == IBV_LINK_LAYER_ETHERNET) {
				/* For raw eth, the MLX4_WQE_CTRL_SOLICIT flag is used
				* to indicate that no icrc should be calculated */
				u.srcrb_flags |= htonl(MLX4_WQE_CTRL_SOLICIT);
				/* For raw eth, take the dmac from the payload */
				u.srcrb_flags16[0] = *(uint16_t *)(uintptr_t)wr->sg_list[0].addr;
				imm = *(uint32_t *)((uintptr_t)(wr->sg_list[0].addr)+2);
			}
			break;

		default:
			break;
		}

		ret = set_data_seg(qp, wqe, &size, !!(exp_send_flags & IBV_EXP_SEND_INLINE),
				   wr->num_sge, wr->sg_list, &inl, owner_bit);
		if (unlikely(ret)) {
			inl = 0;
			*bad_wr = wr;
			goto out;
		}

		set_ctrl_seg(ctrl, (struct ibv_send_wr *)wr, qp, imm, u.srcrb_flags, owner_bit, size, mlx4_wr_op);
		/*
		 * We can improve latency by not stamping the last
		 * send queue WQE until after ringing the doorbell, so
		 * only stamp here if there are still more WQEs to post.
		 */
		if (likely(wr->next))
#ifndef MLX4_WQE_FORMAT
			stamp_send_wqe(qp, (ind + qp->sq_spare_wqes) &
				       (qp->sq.wqe_cnt - 1));
#else
			set_owner_wqe(qp, ind, size, owner_bit);
#endif
		++ind;
	}

out:
	ring_db(qp, ctrl, nreq, size, inl);
	if (likely(nreq))
#ifndef MLX4_WQE_FORMAT
		stamp_send_wqe(qp, (ind + qp->sq_spare_wqes - 1) &
			       (qp->sq.wqe_cnt - 1));
#else
		set_owner_wqe(qp, ind - 1, size, owner_bit);
#endif

	mlx4_unlock(&qp->sq.lock);

	return ret;
}



int mlx4_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
		   struct ibv_recv_wr **bad_wr)
{
	if (likely(wr == NULL && (*bad_wr) != NULL &&
		 (*bad_wr)->wr_id == ERPC_MAGIC_WRID_FOR_FAST_RECV)) {
		/* Handle fast RECV */
		struct mlx4_qp *qp = to_mqp(ibqp);

		int nreq = (*bad_wr)->num_sge;
		qp->rq.head += nreq;

		wmb();
		*qp->db = htonl(qp->rq.head & 0xffff);

		return 0;
	}

	/*
	 * Respond to a "Is driver modded?" probe. A probe is ibv_post_recv() with:
	 * a. A valid queue pair
	 * b. *wr = NULL
	 * c. The @wr_id field of @bad_wr set to ERPC_MODDED_PROBE_WRID
	 *
	 * On a non-modded driver, this call is safe and returns 0. On a modded
	 * driver, this call returns @ERPC_MODDED_PROBE_RET.
	 */
	if (unlikely(ibqp != NULL && wr == NULL && (*bad_wr) != NULL &&
		(*bad_wr)->wr_id == ERPC_MODDED_PROBE_WRID)) {
		/* Tell the caller that this is a modded driver */
		return ERPC_MODDED_PROBE_RET;
	}

	struct mlx4_qp *qp = to_mqp(ibqp);
	struct mlx4_wqe_data_seg *scat;
	int ret = 0;
	int nreq;
	unsigned int ind;
	int i;
	struct mlx4_inlr_rbuff *rbuffs;

	mlx4_lock(&qp->rq.lock);

	/* XXX check that state is OK to post receive */
	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(!(qp->create_flags & IBV_EXP_QP_CREATE_IGNORE_RQ_OVERFLOW) &&
			wq_overflow(&qp->rq, nreq, qp))) {
			ret = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->rq.max_gs)) {
			ret = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		scat = get_recv_wqe(qp, ind);

		for (i = 0; i < wr->num_sge; ++i)
			__set_data_seg(scat + i, wr->sg_list + i);

		if (likely(i < qp->rq.max_gs)) {
			scat[i].byte_count = 0;
			scat[i].lkey       = htonl(MLX4_INVALID_LKEY);
			scat[i].addr       = 0;
		}
		if (qp->max_inlr_sg) {
			rbuffs = qp->inlr_buff.buff[ind].sg_list;
			qp->inlr_buff.buff[ind].list_len = wr->num_sge;
			for (i = 0; i < wr->num_sge; ++i) {
				rbuffs->rbuff = (void *)(unsigned long)(wr->sg_list[i].addr);
				rbuffs->rlen = wr->sg_list[i].length;
				rbuffs++;
			}
		}

		qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (qp->rq.wqe_cnt - 1);
	}

out:
	if (likely(nreq)) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		*qp->db = htonl(qp->rq.head & 0xffff);
	}

	mlx4_unlock(&qp->rq.lock);

	return ret;
}

int num_inline_segs(int data, enum ibv_qp_type type)
{
	/*
	 * Inline data segments are not allowed to cross 64 byte
	 * boundaries.  For UD QPs, the data segments always start
	 * aligned to 64 bytes (16 byte control segment + 48 byte
	 * datagram segment); for other QPs, there will be a 16 byte
	 * control segment and possibly a 16 byte remote address
	 * segment, so in the worst case there will be only 32 bytes
	 * available for the first data segment.
	 */
	if (type == IBV_QPT_UD)
		data += (sizeof (struct mlx4_wqe_ctrl_seg) +
			 sizeof (struct mlx4_wqe_datagram_seg)) %
			MLX4_INLINE_ALIGN;
	else
		data += (sizeof (struct mlx4_wqe_ctrl_seg) +
			 sizeof (struct mlx4_wqe_raddr_seg)) %
			MLX4_INLINE_ALIGN;

	return (data + MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg) - 1) /
		(MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg));
}

void mlx4_calc_sq_wqe_size(struct ibv_qp_cap *cap, enum ibv_qp_type type,
			   struct mlx4_qp *qp)
{
	int size;
	int atomic_size;
	int max_sq_sge;

	max_sq_sge	 = align(cap->max_inline_data +
				 num_inline_segs(cap->max_inline_data, type) *
				 sizeof (struct mlx4_wqe_inline_seg),
				 sizeof (struct mlx4_wqe_data_seg)) /
		sizeof (struct mlx4_wqe_data_seg);
	if (max_sq_sge < cap->max_send_sge)
		max_sq_sge = cap->max_send_sge;

	size = max_sq_sge * sizeof (struct mlx4_wqe_data_seg);
	switch (type) {
	case IBV_QPT_UD:
		size += sizeof (struct mlx4_wqe_datagram_seg);
		break;

	case IBV_QPT_UC:
		size += sizeof (struct mlx4_wqe_raddr_seg);
		break;

	case IBV_QPT_XRC_SEND:
	case IBV_QPT_XRC:
	case IBV_QPT_RC:
		size += sizeof (struct mlx4_wqe_raddr_seg);
		/*
		 * An atomic op will require an atomic segment, a
		 * remote address segment and one scatter entry.
		 */
		atomic_size = (qp->is_masked_atomic ?
			       sizeof(struct mlx4_wqe_masked_atomic_seg) :
			       sizeof(struct mlx4_wqe_atomic_seg)) +
			      sizeof(struct mlx4_wqe_raddr_seg) +
			      sizeof(struct mlx4_wqe_data_seg);

		if (size < atomic_size)
			size = atomic_size;
		break;

	default:
		break;
	}

	/* Make sure that we have enough space for a bind request */
	if (size < sizeof (struct mlx4_wqe_bind_seg))
		size = sizeof (struct mlx4_wqe_bind_seg);

	size += sizeof (struct mlx4_wqe_ctrl_seg);

	for (qp->sq.wqe_shift = 6; 1 << qp->sq.wqe_shift < size;
	     qp->sq.wqe_shift++)
		; /* nothing */
}

int mlx4_use_huge(struct ibv_context *context, const char *key)
{
	char e[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(context, key, e, sizeof(e)) && !strcmp(e, "y"))
		return 1;

	return 0;
}

void mlx4_dealloc_qp_buf(struct ibv_context *context, struct mlx4_qp *qp)
{
	if (qp->rq.wqe_cnt) {
		free(qp->rq.wrid);
		if (qp->max_inlr_sg) {
			free(qp->inlr_buff.buff[0].sg_list);
			free(qp->inlr_buff.buff);
		}
	}
	if (qp->sq.wqe_cnt)
		free(qp->sq.wrid);

	if (qp->buf.hmem != NULL)
		mlx4_free_buf_huge(to_mctx(context), &qp->buf);
	else
		mlx4_free_buf(&qp->buf);
}

void mlx4_set_sq_sizes(struct mlx4_qp *qp, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type)
{
	int wqe_size;
	struct mlx4_context *ctx = to_mctx(qp->verbs_qp.qp.context);

	wqe_size = min((1 << qp->sq.wqe_shift), MLX4_MAX_WQE_SIZE) -
		sizeof (struct mlx4_wqe_ctrl_seg);
	switch (type) {
	case IBV_QPT_UD:
		wqe_size -= sizeof (struct mlx4_wqe_datagram_seg);
		break;

	case IBV_QPT_XRC_SEND:
	case IBV_QPT_XRC:
	case IBV_QPT_UC:
	case IBV_QPT_RC:
		wqe_size -= sizeof (struct mlx4_wqe_raddr_seg);
		break;

	default:
		break;
	}

	qp->sq.max_gs	     = wqe_size / sizeof (struct mlx4_wqe_data_seg);
	cap->max_send_sge    = min(ctx->max_sge, qp->sq.max_gs);
	qp->sq.max_post	     = min(ctx->max_qp_wr,
				   qp->sq.wqe_cnt - qp->sq_spare_wqes);
	cap->max_send_wr     = qp->sq.max_post;

	/*
	 * Inline data segments can't cross a 64 byte boundary.  So
	 * subtract off one segment header for each 64-byte chunk,
	 * taking into account the fact that wqe_size will be 32 mod
	 * 64 for non-UD QPs.
	 */
	qp->max_inline_data  = wqe_size -
		sizeof (struct mlx4_wqe_inline_seg) *
		(align(wqe_size, MLX4_INLINE_ALIGN) / MLX4_INLINE_ALIGN);
	cap->max_inline_data = qp->max_inline_data;
}

struct mlx4_qp *mlx4_find_qp(struct mlx4_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (ctx->qp_table[tind].refcnt)
		return ctx->qp_table[tind].table[qpn & ctx->qp_table_mask];
	else
		return NULL;
}

int mlx4_store_qp(struct mlx4_context *ctx, uint32_t qpn, struct mlx4_qp *qp)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (!ctx->qp_table[tind].refcnt) {
		ctx->qp_table[tind].table = calloc(ctx->qp_table_mask + 1,
						   sizeof (struct mlx4_qp *));
		if (!ctx->qp_table[tind].table)
			return -1;
	}

	++ctx->qp_table[tind].refcnt;
	ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = qp;
	return 0;
}

void mlx4_clear_qp(struct mlx4_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (!--ctx->qp_table[tind].refcnt)
		free(ctx->qp_table[tind].table);
	else
		ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = NULL;
}

int mlx4_post_task(struct ibv_context *context,
		   struct ibv_exp_task *task_list,
		   struct ibv_exp_task **bad_task)
{
	int rc = 0;
	struct ibv_exp_task *cur_task = NULL;
	struct ibv_exp_send_wr  *bad_wr;
	struct mlx4_context *mlx4_ctx = to_mctx(context);

	if (!task_list)
		return rc;

	pthread_mutex_lock(&mlx4_ctx->task_mutex);

	cur_task = task_list;
	while (!rc && cur_task) {

		switch (cur_task->task_type) {
		case IBV_EXP_TASK_SEND:
			rc = ibv_exp_post_send(cur_task->item.qp,
					       cur_task->item.send_wr,
					       &bad_wr);
			break;

		case IBV_EXP_TASK_RECV:
			rc = ibv_post_recv(cur_task->item.qp,
					cur_task->item.recv_wr,
					NULL);
			break;

		default:
			rc = -1;
		}

		if (rc && bad_task) {
			*bad_task = cur_task;
			break;
		}

		cur_task = cur_task->next;
	}

	pthread_mutex_unlock(&mlx4_ctx->task_mutex);

	return rc;
}

/*
 * family interfaces functions
 */

/*
 * send_pending - is a general post send function that put one message in
 * the send queue. The function is not ringing the QP door-bell.
 *
 * User may call this function several times to fill send queue with
 * several messages, then he can call mlx4_send_flush to ring the QP DB
 *
 * This function is used to implement the following QP burst family functions:
 * - send_pending
 * - send_pending_inline
 * - send_pending_sg_list
 * - send_burst
 */
static inline int send_pending(struct ibv_qp *ibqp, uint64_t addr,
			       uint32_t length, uint32_t lkey,
			       uint32_t flags,
			       const int use_raw_eth, const int use_inl,
			       const int thread_safe, const int wqe_64,
			       const int use_sg_list, int num_sge,
			       struct ibv_sge *sg_list,
			       const int lb) __attribute__((always_inline));
static inline int send_pending(struct ibv_qp *ibqp, uint64_t addr,
			       uint32_t length, uint32_t lkey,
			       uint32_t flags,
			       const int use_raw_eth, const int use_inl,
			       const int thread_safe, const int wqe_64,
			       const int use_sg_list, int num_sge,
			       struct ibv_sge *sg_list,
			       const int lb)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	struct mlx4_wqe_ctrl_seg *ctrl;
	struct mlx4_wqe_data_seg *dseg;
	uint32_t tunnel_offload = 0;
	unsigned int owner_bit = qp->sq.head & qp->sq.wqe_cnt ? htonl(WQE_CTRL_OWN) : 0;
	int size;
	int idx;
	int i;

	if (thread_safe)
		mlx4_lock(&qp->sq.lock);

	if (wqe_64)
		ctrl = get_send_wqe64(qp, qp->sq.head & (qp->sq.wqe_cnt - 1));
	else
		ctrl = get_send_wqe(qp, qp->sq.head & (qp->sq.wqe_cnt - 1));

	dseg = (struct mlx4_wqe_data_seg *)(((char *)ctrl) + sizeof(struct mlx4_wqe_ctrl_seg));

	if (use_sg_list) {
		for (i =  num_sge - 1; i >= 0 ; --i)
				set_ptr_data(dseg + i, sg_list + i, owner_bit);

		size = (sizeof(struct mlx4_wqe_ctrl_seg) +  (num_sge * sizeof(struct mlx4_wqe_data_seg)))/ 16;
	} else {
		if (use_inl) {
			size = sizeof(struct mlx4_wqe_ctrl_seg) / 16;
			set_data_inl_seg_fast(qp, (void *)(uintptr_t)addr, length, dseg, &size, owner_bit);
		} else {
			size = (sizeof(struct mlx4_wqe_ctrl_seg) +  sizeof(struct mlx4_wqe_data_seg))/ 16;
			dseg->byte_count = SET_BYTE_COUNT(length);
			dseg->lkey = htonl(lkey);
			dseg->addr = htonll(addr);
		}
	}

	if (use_raw_eth) {
		/* For raw eth, the SOLICIT flag is used
		* to indicate that no icrc should be calculated */
		idx = IBV_EXP_QP_BURST_SOLICITED |
		      (flags & (IBV_EXP_QP_BURST_SIGNALED |
				IBV_EXP_QP_BURST_IP_CSUM |
				IBV_EXP_QP_BURST_TUNNEL));
		tunnel_offload = flags & IBV_EXP_QP_BURST_TUNNEL ? MLX4_WQE_CTRL_IIP | MLX4_WQE_CTRL_IL4 : 0;
	} else {
		idx = (flags & (IBV_EXP_QP_BURST_SIGNALED |
				IBV_EXP_QP_BURST_SOLICITED |
				IBV_EXP_QP_BURST_IP_CSUM));
	}

	if (use_raw_eth && lb) {
		union {
			uint32_t srcrb_flags;
			uint16_t srcrb_flags16[2];
		} u;

		u.srcrb_flags = htonl((uint32_t)qp->srcrb_flags_tbl[idx]);
		/* For raw eth, take the dmac from the payload */
		if (use_sg_list)
			addr = sg_list[0].addr;
		u.srcrb_flags16[0] = *(uint16_t *)(uintptr_t)addr;
		ctrl->srcrb_flags = u.srcrb_flags;
		ctrl->imm = *(uint32_t *)((uintptr_t)(addr)+2);
	} else {
		ctrl->srcrb_flags = htonl((uint32_t)qp->srcrb_flags_tbl[idx]);
		ctrl->imm = 0;
	}
	ctrl->fence_size = (flags & IBV_EXP_QP_BURST_FENCE ? MLX4_WQE_CTRL_FENCE : 0) | size;

	/*
	 * Make sure descriptor is fully written before
	 * setting ownership bit (because HW can start
	 * executing as soon as we do).
	 */
	wmb();

	ctrl->owner_opcode = htonl(MLX4_OPCODE_SEND | tunnel_offload) | owner_bit;
	qp->sq.head++;

	if (!wqe_64)
#ifndef MLX4_WQE_FORMAT
		stamp_send_wqe(qp, (qp->sq.head + qp->sq_spare_wqes) &
			       (qp->sq.wqe_cnt - 1));
#else
		set_owner_wqe(qp, qp->sq.head, size, owner_bit);
#endif
	if (thread_safe)
		mlx4_unlock(&qp->sq.lock);
	else
		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

	return 0;
}

/* burst family - send_pending */
static inline int mlx4_send_pending_safe(struct ibv_qp *qp, uint64_t addr,
					 uint32_t length, uint32_t lkey,
					 uint32_t flags, const int lb) __attribute__((always_inline));
static inline int mlx4_send_pending_safe(struct ibv_qp *qp, uint64_t addr,
					 uint32_t length, uint32_t lkey,
					 uint32_t flags, const int lb)
{
	struct mlx4_qp *mqp = to_mqp(qp);
	int raw_eth = mqp->qp_type == IBV_QPT_RAW_PACKET &&
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;
	int wqe_64 = mqp->sq.wqe_shift == 6;

			/*  qp, addr, length, lkey, flags, raw_eth, inl, safe,	*/
	return send_pending(qp, addr, length, lkey, flags, raw_eth, 0,   1,
			/*  wqe_64, use_sg, num_sge, sg_list, lb		*/
			    wqe_64, 0,      0,       NULL,    lb);
}

static int mlx4_send_pending_safe_lb(struct ibv_qp *qp, uint64_t addr, uint32_t length, uint32_t lkey, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_pending_safe_lb(struct ibv_qp *qp, uint64_t addr, uint32_t length, uint32_t lkey, uint32_t flags)
{
	return mlx4_send_pending_safe(qp, addr, length, lkey, flags, 1);
}

static int mlx4_send_pending_safe_no_lb(struct ibv_qp *qp, uint64_t addr, uint32_t length, uint32_t lkey, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_pending_safe_no_lb(struct ibv_qp *qp, uint64_t addr, uint32_t length, uint32_t lkey, uint32_t flags)
{
	return mlx4_send_pending_safe(qp, addr, length, lkey, flags, 0);
}

#define MLX4_SEND_PENDING_UNSAFE_NAME(eth, wqe64, lb) mlx4_send_pending_unsafe_##eth##wqe64##lb
#define MLX4_SEND_PENDING_UNSAFE(eth, wqe64, lb)				\
	static int MLX4_SEND_PENDING_UNSAFE_NAME(eth, wqe64, lb)(		\
					struct ibv_qp *qp, uint64_t addr,	\
					uint32_t length, uint32_t lkey,		\
					uint32_t flags) __MLX4_ALGN_FUNC__;	\
	static int MLX4_SEND_PENDING_UNSAFE_NAME(eth, wqe64, lb)(		\
					struct ibv_qp *qp, uint64_t addr,	\
					uint32_t length, uint32_t lkey,		\
					uint32_t flags)				\
	{									\
		/*                  qp, addr, length, lkey, flags, eth, inl, */	\
		return send_pending(qp, addr, length, lkey, flags, eth, 0,	\
				/*  safe,  wqe_64, use_sg, num_sge, sg_list  */	\
				    0,     wqe64,  0,      0,       NULL,	\
				/*  lb					     */ \
				    lb);					\
	}
/*			 eth, wqe64, lb */
MLX4_SEND_PENDING_UNSAFE(0,   0,     0);
MLX4_SEND_PENDING_UNSAFE(0,   0,     1);
MLX4_SEND_PENDING_UNSAFE(0,   1,     0);
MLX4_SEND_PENDING_UNSAFE(0,   1,     1);
MLX4_SEND_PENDING_UNSAFE(1,   0,     0);
MLX4_SEND_PENDING_UNSAFE(1,   0,     1);
MLX4_SEND_PENDING_UNSAFE(1,   1,     0);
MLX4_SEND_PENDING_UNSAFE(1,   1,     1);

/* burst family - send_pending_inline */
static inline int mlx4_send_pending_inl_safe(struct ibv_qp *qp, void *addr,
					     uint32_t length, uint32_t flags,
					     const int lb) __attribute__((always_inline));
static inline int mlx4_send_pending_inl_safe(struct ibv_qp *qp, void *addr,
					     uint32_t length, uint32_t flags,
					     const int lb)
{
	struct mlx4_qp *mqp = to_mqp(qp);
	int raw_eth = mqp->qp_type == IBV_QPT_RAW_PACKET && mqp->link_layer == IBV_LINK_LAYER_ETHERNET;
	int wqe_64 = mqp->sq.wqe_shift == 6;

			/*  qp, addr,            length, lkey, flags, raw_eth,	*/
	return send_pending(qp, (uintptr_t)addr, length, 0,    flags, raw_eth,
			/*  inl, safe,  wqe_64, use_sg, num_sge, sg_list, lb	*/
			    1,   1,     wqe_64, 0,      0,       NULL,    lb);
}

static int mlx4_send_pending_inl_safe_lb(struct ibv_qp *qp, void *addr, uint32_t length, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_pending_inl_safe_lb(struct ibv_qp *qp, void *addr, uint32_t length, uint32_t flags)
{
	return mlx4_send_pending_inl_safe(qp, addr, length, flags, 1);
}

static int mlx4_send_pending_inl_safe_no_lb(struct ibv_qp *qp, void *addr, uint32_t length, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_pending_inl_safe_no_lb(struct ibv_qp *qp, void *addr, uint32_t length, uint32_t flags)
{
	return mlx4_send_pending_inl_safe(qp, addr, length, flags, 0);
}

#define MLX4_SEND_PENDING_INL_UNSAFE_NAME(eth, wqe64, lb) mlx4_send_pending_inl_unsafe_##eth##wqe64##lb
#define MLX4_SEND_PENDING_INL_UNSAFE(eth, wqe64, lb)						\
	static int MLX4_SEND_PENDING_INL_UNSAFE_NAME(eth, wqe64, lb)(				\
					struct ibv_qp *qp, void *addr,				\
					uint32_t length, uint32_t flags) __MLX4_ALGN_FUNC__;	\
	static int MLX4_SEND_PENDING_INL_UNSAFE_NAME(eth, wqe64, lb)(				\
					struct ibv_qp *qp, void *addr,				\
					uint32_t length, uint32_t flags)			\
	{											\
		/*                  qp, addr,            length, lkey, flags, eth, inl, */	\
		return send_pending(qp, (uintptr_t)addr, length, 0,    flags, eth, 1,		\
				/*  safe,  wqe_64, use_sg, num_sge, sg_list, lb */		\
				    0,     wqe64,  0,      0,       NULL,    lb);		\
	}
/*			   eth, wqe64, lb */
MLX4_SEND_PENDING_INL_UNSAFE(0,   0,   0);
MLX4_SEND_PENDING_INL_UNSAFE(0,   0,   1);
MLX4_SEND_PENDING_INL_UNSAFE(0,   1,   0);
MLX4_SEND_PENDING_INL_UNSAFE(0,   1,   1);
MLX4_SEND_PENDING_INL_UNSAFE(1,   0,   0);
MLX4_SEND_PENDING_INL_UNSAFE(1,   0,   1);
MLX4_SEND_PENDING_INL_UNSAFE(1,   1,   0);
MLX4_SEND_PENDING_INL_UNSAFE(1,   1,   1);

/* burst family - send_pending_sg_list */
static inline int mlx4_send_pending_sg_list_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags, const int lb) __attribute__((always_inline));
static inline int mlx4_send_pending_sg_list_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags, const int lb)
{
	struct mlx4_qp *mqp = to_mqp(ibqp);
	int raw_eth = mqp->qp_type == IBV_QPT_RAW_PACKET && mqp->link_layer == IBV_LINK_LAYER_ETHERNET;
	int wqe_64 = mqp->sq.wqe_shift == 6;

			/*  qp,   addr, length, lkey, flags, raw_eth, inl,	*/
	return send_pending(ibqp, 0,    0,      0,    flags, raw_eth, 0,
			/*  safe,  wqe_64, use_sg, num_sge, sg_list, lb */
			    1,     wqe_64, 1,      num,     sg_list, lb);
}
static int mlx4_send_pending_sg_list_safe_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_pending_sg_list_safe_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags)
{
	return mlx4_send_pending_sg_list_safe(ibqp, sg_list, num, flags, 1);
}

static int mlx4_send_pending_sg_list_safe_no_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_pending_sg_list_safe_no_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags)
{
	return mlx4_send_pending_sg_list_safe(ibqp, sg_list, num, flags, 0);
}

#define MLX4_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, wqe64, lb) mlx4_send_pending_sg_list_unsafe_##eth##wqe64##lb
#define MLX4_SEND_PENDING_SG_LIST_UNSAFE(eth, wqe64, lb)					\
	static int MLX4_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, wqe64, lb)(			\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags) __MLX4_ALGN_FUNC__;	\
	static int MLX4_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, wqe64, lb)(			\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags)				\
	{											\
				/*  qp,   addr, length, lkey, flags, eth, inl, */			\
		return send_pending(ibqp, 0,    0,      0,    flags, eth, 0,			\
				/*  safe,  wqe_64, use_sg, num_sge, sg_list,  lb */		\
				    0,     wqe64,  1,      num,       sg_list, lb);		\
	}
/*			         eth, wqe64, lb */
MLX4_SEND_PENDING_SG_LIST_UNSAFE(0,     0,   0);
MLX4_SEND_PENDING_SG_LIST_UNSAFE(0,     0,   1);
MLX4_SEND_PENDING_SG_LIST_UNSAFE(0,     1,   0);
MLX4_SEND_PENDING_SG_LIST_UNSAFE(0,     1,   1);
MLX4_SEND_PENDING_SG_LIST_UNSAFE(1,     0,   0);
MLX4_SEND_PENDING_SG_LIST_UNSAFE(1,     0,   1);
MLX4_SEND_PENDING_SG_LIST_UNSAFE(1,     1,   0);
MLX4_SEND_PENDING_SG_LIST_UNSAFE(1,     1,   1);

static inline int send_flush_unsafe(struct ibv_qp *ibqp, const int _1thrd_evict, const int wqe64) __attribute__((always_inline));
/* burst family - send_burst */
static inline int send_msg_list(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
				uint32_t flags, const int raw_eth, const int thread_safe,
				const int wqe_64, const int use_bf, const int _1thrd_evict, const int lb) __attribute__((always_inline));
static inline int send_msg_list(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
				uint32_t flags, const int raw_eth, const int thread_safe,
				const int wqe_64, const int use_bf, const int _1thrd_evict, const int lb)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	int i;

	if (unlikely(thread_safe))
		mlx4_lock(&qp->sq.lock);

	for (i = 0; i < num; i++, sg_list++)
			/*   qp,   addr,          length,          lkey,	*/
		send_pending(ibqp, sg_list->addr, sg_list->length, sg_list->lkey,
			/*   flags, raw_eth, inl, safe,  wqe_64, use_sg,	*/
			     flags, raw_eth, 0,   0,     wqe_64, 0,
			/*   num_sge, sg_list, lb				*/
			     0,       NULL,    lb);

	if (use_bf)
		/* use send_flush_unsafe since lock is already taken if needed */
		send_flush_unsafe(ibqp, _1thrd_evict, wqe_64);
	else
		mmio_writel((unsigned long)(qp->sdb), qp->doorbell_qpn);

	if (unlikely(thread_safe))
		mlx4_unlock(&qp->sq.lock);

	return 0;
}

static inline int mlx4_send_burst_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags, const int lb) __attribute__((always_inline));
static inline int mlx4_send_burst_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags, const int lb)
{
	struct mlx4_qp *mqp = to_mqp(ibqp);
	int raw_eth = mqp->qp_type == IBV_QPT_RAW_PACKET && mqp->link_layer == IBV_LINK_LAYER_ETHERNET;
	int wqe_64 = mqp->sq.wqe_shift == 6;
	int _1thrd_evict = mqp->db_method == MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_PB ||
			   mqp->db_method == MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_NPB;
	int use_bf = mqp->db_method != MLX4_QP_DB_METHOD_DB;

	return send_msg_list(ibqp, sg_list, num, flags, raw_eth, 1, wqe_64, use_bf, _1thrd_evict, lb);
}

static int mlx4_send_burst_safe_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_burst_safe_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags)
{
	return mlx4_send_burst_safe(ibqp, sg_list, num, flags, 1);
}

static int mlx4_send_burst_safe_no_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags) __MLX4_ALGN_FUNC__;
static int mlx4_send_burst_safe_no_lb(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags)
{
	return mlx4_send_burst_safe(ibqp, sg_list, num, flags, 0);
}

#define MLX4_SEND_BURST_UNSAFE_NAME(_1thrd_evict, eth, wqe64, lb) mlx4_send_burst_unsafe_##_1thrd_evict##eth##wqe64##lb
#define MLX4_SEND_BURST_UNSAFE(_1thrd_evict, eth, wqe64, lb)					\
	static int MLX4_SEND_BURST_UNSAFE_NAME(_1thrd_evict, eth, wqe64, lb)(			\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags) __MLX4_ALGN_FUNC__;	\
	static int MLX4_SEND_BURST_UNSAFE_NAME(_1thrd_evict, eth, wqe64, lb)(			\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags)				\
	{											\
		return send_msg_list(ibqp, sg_list, num, flags, eth, 0, wqe64, 1, _1thrd_evict,	\
				     lb);							\
	}
/*	     _1thrd_evict, eth, wqe64, lb */
MLX4_SEND_BURST_UNSAFE(0,   0,   0,    0);
MLX4_SEND_BURST_UNSAFE(0,   0,   0,    1);
MLX4_SEND_BURST_UNSAFE(0,   0,   1,    0);
MLX4_SEND_BURST_UNSAFE(0,   0,   1,    1);
MLX4_SEND_BURST_UNSAFE(0,   1,   0,    0);
MLX4_SEND_BURST_UNSAFE(0,   1,   0,    1);
MLX4_SEND_BURST_UNSAFE(0,   1,   1,    0);
MLX4_SEND_BURST_UNSAFE(0,   1,   1,    1);
MLX4_SEND_BURST_UNSAFE(1,   0,   0,    0);
MLX4_SEND_BURST_UNSAFE(1,   0,   0,    1);
MLX4_SEND_BURST_UNSAFE(1,   0,   1,    0);
MLX4_SEND_BURST_UNSAFE(1,   0,   1,    1);
MLX4_SEND_BURST_UNSAFE(1,   1,   0,    0);
MLX4_SEND_BURST_UNSAFE(1,   1,   0,    1);
MLX4_SEND_BURST_UNSAFE(1,   1,   1,    0);
MLX4_SEND_BURST_UNSAFE(1,   1,   1,    1);

#define MLX4_SEND_BURST_UNSAFE_DB_NAME(eth, wqe64, lb) mlx4_send_burst_unsafe_##eth##wqe64##lb
#define MLX4_SEND_BURST_UNSAFE_DB(eth, wqe64, lb)						\
	static int MLX4_SEND_BURST_UNSAFE_DB_NAME(eth, wqe64, lb)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags) __MLX4_ALGN_FUNC__;	\
	static int MLX4_SEND_BURST_UNSAFE_DB_NAME(eth, wqe64, lb)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags)				\
	{											\
		return send_msg_list(ibqp, sg_list, num, flags, eth, 0, wqe64, 0, 0, lb);	\
	}
/*	                    eth, wqe64, lb */
MLX4_SEND_BURST_UNSAFE_DB(0,   0,    0);
MLX4_SEND_BURST_UNSAFE_DB(0,   0,    1);
MLX4_SEND_BURST_UNSAFE_DB(0,   1,    0);
MLX4_SEND_BURST_UNSAFE_DB(0,   1,    1);
MLX4_SEND_BURST_UNSAFE_DB(1,   0,    0);
MLX4_SEND_BURST_UNSAFE_DB(1,   0,    1);
MLX4_SEND_BURST_UNSAFE_DB(1,   1,    0);
MLX4_SEND_BURST_UNSAFE_DB(1,   1,    1);

/* burst family - send_flush */
static int mlx4_send_flush_db(struct ibv_qp *ibqp) __MLX4_ALGN_FUNC__;
static int mlx4_send_flush_db(struct ibv_qp *ibqp)
{
	struct mlx4_qp *qp = to_mqp(ibqp);

	mmio_writel((unsigned long)(qp->sdb), qp->doorbell_qpn);

	return 0;
}

static inline int send_flush_unsafe(struct ibv_qp *ibqp, const int _1thrd_evict, const int wqe64)
{
	struct mlx4_qp *qp = to_mqp(ibqp);

	if (qp->last_db_head + 1 == qp->sq.head) {
		struct mlx4_wqe_ctrl_seg *ctrl = get_send_wqe(qp, qp->last_db_head & (qp->sq.wqe_cnt - 1));
		int size = ctrl->fence_size & 0x3f;

		/*
		 * There is no need to check that size > 1 since we get here only
		 * after using send_pending function, this guarantee that size > 1
		 */
		if (wqe64)
			copy_wqe_to_bf(qp, ctrl, 64, qp->last_db_head,
				       1, _1thrd_evict);
		else if (size <= qp->bf_buf_size / 16)
			copy_wqe_to_bf(qp, ctrl, align(size * 16, 64),
				       qp->last_db_head,
				       1, _1thrd_evict);
		else
			mmio_writel((unsigned long)(qp->sdb), qp->doorbell_qpn);
	} else {
		mmio_writel((unsigned long)(qp->sdb), qp->doorbell_qpn);
	}
	qp->last_db_head = qp->sq.head;

	return 0;
}

#define MLX4_SEND_FLUSH_UNSAFE_NAME(_1thrd_evict, wqe64) mlx4_send_flush_unsafe_##_1thrd_evict##wqe64
#define MLX4_SEND_FLUSH_UNSAFE(_1thrd_evict, wqe64)						\
	static int MLX4_SEND_FLUSH_UNSAFE_NAME(_1thrd_evict, wqe64)(				\
					struct ibv_qp *ibqp) __MLX4_ALGN_FUNC__;		\
	static int MLX4_SEND_FLUSH_UNSAFE_NAME(_1thrd_evict, wqe64)(				\
					struct ibv_qp *ibqp)					\
	{											\
		return send_flush_unsafe(ibqp, _1thrd_evict, wqe64);				\
	}

/*	      _1thrd_evict, wqe64 */
MLX4_SEND_FLUSH_UNSAFE(0,   0);
MLX4_SEND_FLUSH_UNSAFE(1,   0);
MLX4_SEND_FLUSH_UNSAFE(0,   1);
MLX4_SEND_FLUSH_UNSAFE(1,   1);

/* burst family - recv_burst */
static inline int recv_burst(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
			     const int thread_safe, const int use_inlne_recv, const int max_one_sge) __attribute__((always_inline));
static inline int recv_burst(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
			     const int thread_safe, const int use_inlne_recv, const int max_one_sge)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	struct mlx4_wqe_data_seg *scat;
	struct mlx4_inlr_rbuff *rbuffs;
	unsigned int ind;
	int i;

	if (thread_safe)
		mlx4_lock(&qp->rq.lock);

	for (i = 0; i < num; ++i) {
		ind = qp->rq.head & (qp->rq.wqe_cnt - 1);
		scat = get_recv_wqe(qp, ind);
		__set_data_seg(scat, sg_list);

		if (!max_one_sge) {
			scat[1].byte_count = 0;
			scat[1].lkey       = htonl(MLX4_INVALID_LKEY);
			scat[1].addr       = 0;
		}

		if (use_inlne_recv) {
			rbuffs = qp->inlr_buff.buff[ind].sg_list;
			qp->inlr_buff.buff[ind].list_len = 1;
			rbuffs->rbuff = (void *)(unsigned long)(sg_list->addr);
			rbuffs->rlen = sg_list->length;
			rbuffs++;
		}
		sg_list++;
		qp->rq.head++;
	}

	/*
	 * Make sure that descriptors are written before
	 * doorbell record.
	 */
	wmb();

	*qp->db = htonl(qp->rq.head & 0xffff);

	if (thread_safe)
		mlx4_unlock(&qp->rq.lock);

	return 0;
}

static int mlx4_recv_burst_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num) __MLX4_ALGN_FUNC__;
static int mlx4_recv_burst_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num)
{
	struct mlx4_qp *qp = to_mqp(ibqp);

	return recv_burst(ibqp, sg_list, num, 1, qp->max_inlr_sg, qp->rq.max_gs == 1);
}
#define MLX4_RECV_BURST_UNSAFE_NAME(inlr, _1sge) mlx4_recv_burst_unsafe_##inlr##_1sge
#define MLX4_RECV_BURST_UNSAFE(inlr, _1sge)						\
	static int MLX4_RECV_BURST_UNSAFE_NAME(inlr, _1sge)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,	\
					uint32_t num) __MLX4_ALGN_FUNC__;		\
	static int MLX4_RECV_BURST_UNSAFE_NAME(inlr, _1sge)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,	\
					uint32_t num)					\
	{										\
		return recv_burst(ibqp, sg_list, num, 0, inlr, _1sge);			\
	}
/*		       inlr, _1sge */
MLX4_RECV_BURST_UNSAFE(0,    0);
MLX4_RECV_BURST_UNSAFE(1,    0);
MLX4_RECV_BURST_UNSAFE(0,    1);
MLX4_RECV_BURST_UNSAFE(1,    1);

/*
 * qp_burst family implementation for safe QP
 */
struct ibv_exp_qp_burst_family mlx4_qp_burst_family_safe_lb = {
		.send_burst = mlx4_send_burst_safe_lb,
		.send_pending = mlx4_send_pending_safe_lb,
		.send_pending_inline = mlx4_send_pending_inl_safe_lb,
		.send_pending_sg_list = mlx4_send_pending_sg_list_safe_lb,
		.recv_burst = mlx4_recv_burst_safe,
		.send_flush = mlx4_send_flush_db
};

struct ibv_exp_qp_burst_family mlx4_qp_burst_family_safe_no_lb = {
		.send_burst = mlx4_send_burst_safe_no_lb,
		.send_pending = mlx4_send_pending_safe_no_lb,
		.send_pending_inline = mlx4_send_pending_inl_safe_no_lb,
		.send_pending_sg_list = mlx4_send_pending_sg_list_safe_no_lb,
		.recv_burst = mlx4_recv_burst_safe,
		.send_flush = mlx4_send_flush_db
};

/*
 * qp_burst family implementation table for unsafe QP
 */
#define MLX4_QP_BURST_UNSAFE_TBL_IDX(lb, _1thrd_evict, eth, wqe64, inlr, _1sge)	\
		(lb << 5 | _1thrd_evict << 4 | eth << 3 | wqe64 << 2 | inlr << 1 | _1sge)

#define MLX4_QP_BURST_UNSAFE_TBL_ENTRY(lb, _1thrd_evict, eth, wqe64, inlr, _1sge)			\
	[MLX4_QP_BURST_UNSAFE_TBL_IDX(lb, _1thrd_evict, eth, wqe64, inlr, _1sge)] = {			\
		.send_burst		= MLX4_SEND_BURST_UNSAFE_NAME(_1thrd_evict, eth, wqe64, lb),	\
		.send_pending		= MLX4_SEND_PENDING_UNSAFE_NAME(eth, wqe64, lb),		\
		.send_pending_inline	= MLX4_SEND_PENDING_INL_UNSAFE_NAME(eth, wqe64, lb),		\
		.send_pending_sg_list	= MLX4_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, wqe64, lb),	\
		.recv_burst		= MLX4_RECV_BURST_UNSAFE_NAME(inlr, _1sge),			\
		.send_flush		= MLX4_SEND_FLUSH_UNSAFE_NAME(_1thrd_evict, wqe64),		\
	}
static struct ibv_exp_qp_burst_family mlx4_qp_burst_family_unsafe_tbl[1 << 6] = {
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 0, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 0, 1, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 0, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(0, 1, 1, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 0, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 0, 1, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 0, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_TBL_ENTRY(1, 1, 1, 1, 1, 1),
};

#define MLX4_QP_BURST_UNSAFE_DB_TBL_IDX(lb, eth, wqe64, inlr, _1sge)	\
		(lb << 4 | eth << 3 | wqe64 << 2 | inlr << 1 | _1sge)

#define MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(lb, eth, wqe64, inlr, _1sge)				\
	[MLX4_QP_BURST_UNSAFE_DB_TBL_IDX(lb, eth, wqe64, inlr, _1sge)] = {				\
		.send_burst		= MLX4_SEND_BURST_UNSAFE_DB_NAME(eth, wqe64, lb),		\
		.send_pending		= MLX4_SEND_PENDING_UNSAFE_NAME(eth, wqe64, lb),		\
		.send_pending_inline	= MLX4_SEND_PENDING_INL_UNSAFE_NAME(eth, wqe64, lb),		\
		.send_pending_sg_list	= MLX4_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, wqe64, lb),	\
		.recv_burst		= MLX4_RECV_BURST_UNSAFE_NAME(inlr, _1sge),			\
		.send_flush		= mlx4_send_flush_db,						\
	}
static struct ibv_exp_qp_burst_family mlx4_qp_burst_family_unsafe_db_tbl[1 << 5] = {
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 0, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(0, 1, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 0, 1, 1, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 0, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 0, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 0, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 0, 1, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 1, 0, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 1, 0, 1),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 1, 1, 0),
		MLX4_QP_BURST_UNSAFE_DB_TBL_ENTRY(1, 1, 1, 1, 1),
};

struct ibv_exp_qp_burst_family *mlx4_get_qp_burst_family(struct mlx4_qp *qp,
							 struct ibv_exp_query_intf_params *params,
							 enum ibv_exp_query_intf_status *status)
{
	enum ibv_exp_query_intf_status ret = IBV_EXP_INTF_STAT_OK;
	struct ibv_exp_qp_burst_family *family = NULL;
	uint32_t unsupported_f;

	if ((qp->verbs_qp.qp.state < IBV_QPS_INIT) || (qp->verbs_qp.qp.state > IBV_QPS_RTS)) {
			*status = IBV_EXP_INTF_STAT_INVAL_OBJ_STATE;
			return NULL;
	}

	if (params->flags) {
		fprintf(stderr, PFX "Global interface flags(0x%x) are not supported for QP family\n", params->flags);
		*status = IBV_EXP_INTF_STAT_FLAGS_NOT_SUPPORTED;

		return NULL;
	}
	unsupported_f = params->family_flags & ~(IBV_EXP_QP_BURST_CREATE_DISABLE_ETH_LOOPBACK |
						 IBV_EXP_QP_BURST_CREATE_ENABLE_MULTI_PACKET_SEND_WR);
	if (unsupported_f) {
		fprintf(stderr, PFX "Family flags(0x%x) are not supported for QP family\n", unsupported_f);
		*status = IBV_EXP_INTF_STAT_FAMILY_FLAGS_NOT_SUPPORTED;

		return NULL;
	}

	switch (qp->qp_type) {
	case IBV_QPT_RC:
	case IBV_QPT_UC:
	case IBV_QPT_RAW_PACKET:
		if (qp->model_flags & MLX4_QP_MODEL_FLAG_THREAD_SAFE) {
			int lb = !(params->family_flags & IBV_EXP_QP_BURST_CREATE_DISABLE_ETH_LOOPBACK);

			if (lb)
				family = &mlx4_qp_burst_family_safe_lb;
			else
				family = &mlx4_qp_burst_family_safe_no_lb;
		} else {
			int eth = qp->qp_type == IBV_QPT_RAW_PACKET &&
				  qp->link_layer == IBV_LINK_LAYER_ETHERNET;
			int wqe64 = qp->sq.wqe_shift == 6;
			int inlr = qp->max_inlr_sg != 0;
			int _1sge = qp->rq.max_gs == 1;
			int _1thrd_evict = qp->db_method == MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_PB ||
					   qp->db_method == MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_NPB;
			int lb = !(params->family_flags & IBV_EXP_QP_BURST_CREATE_DISABLE_ETH_LOOPBACK);

			if (qp->db_method == MLX4_QP_DB_METHOD_DB)
				family = &mlx4_qp_burst_family_unsafe_db_tbl
					[MLX4_QP_BURST_UNSAFE_DB_TBL_IDX(lb, eth, wqe64, inlr, _1sge)];
			else
				family = &mlx4_qp_burst_family_unsafe_tbl
					[MLX4_QP_BURST_UNSAFE_TBL_IDX(lb, _1thrd_evict, eth, wqe64, inlr, _1sge)];
		}
		break;

	default:
		ret = IBV_EXP_INTF_STAT_INVAL_PARARM;
		break;
	}

	*status = ret;

	return family;
}
