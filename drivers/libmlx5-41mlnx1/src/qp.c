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

#include <stdlib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "mlx5.h"
#include "doorbell.h"
#include "wqe.h"

enum {
	MLX5_OPCODE_BASIC	= 0x00010000,
	MLX5_OPCODE_MANAGED	= 0x00020000,

	MLX5_OPCODE_WITH_IMM	= 0x01000000,
	MLX5_OPCODE_EXT_ATOMICS = 0x08,
};

#define MLX5_IB_OPCODE(op, class, attr)     (((class) & 0x00FF0000) | ((attr) & 0xFF000000) | ((op) & 0x0000FFFF))
#define MLX5_IB_OPCODE_GET_CLASS(opcode)    ((opcode) & 0x00FF0000)
#define MLX5_IB_OPCODE_GET_OP(opcode)       ((opcode) & 0x0000FFFF)
#define MLX5_IB_OPCODE_GET_ATTR(opcode)     ((opcode) & 0xFF000000)


static const uint32_t mlx5_ib_opcode[] = {
	[IBV_EXP_WR_SEND]                       = MLX5_IB_OPCODE(MLX5_OPCODE_SEND,                MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_SEND_WITH_IMM]              = MLX5_IB_OPCODE(MLX5_OPCODE_SEND_IMM,            MLX5_OPCODE_BASIC, MLX5_OPCODE_WITH_IMM),
	[IBV_EXP_WR_SEND_WITH_INV]		= MLX5_IB_OPCODE(MLX5_OPCODE_SEND_INVAL,          MLX5_OPCODE_BASIC, MLX5_OPCODE_WITH_IMM),
	[IBV_EXP_WR_RDMA_WRITE]                 = MLX5_IB_OPCODE(MLX5_OPCODE_RDMA_WRITE,          MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_RDMA_WRITE_WITH_IMM]        = MLX5_IB_OPCODE(MLX5_OPCODE_RDMA_WRITE_IMM,      MLX5_OPCODE_BASIC, MLX5_OPCODE_WITH_IMM),
	[IBV_EXP_WR_RDMA_READ]                  = MLX5_IB_OPCODE(MLX5_OPCODE_RDMA_READ,           MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_ATOMIC_CMP_AND_SWP]         = MLX5_IB_OPCODE(MLX5_OPCODE_ATOMIC_CS,           MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_ATOMIC_FETCH_AND_ADD]       = MLX5_IB_OPCODE(MLX5_OPCODE_ATOMIC_FA,           MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP]   = MLX5_IB_OPCODE(MLX5_OPCODE_ATOMIC_MASKED_CS,  MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD] = MLX5_IB_OPCODE(MLX5_OPCODE_ATOMIC_MASKED_FA,  MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_SEND_ENABLE]                = MLX5_IB_OPCODE(MLX5_OPCODE_SEND_ENABLE,         MLX5_OPCODE_MANAGED, 0),
	[IBV_EXP_WR_RECV_ENABLE]                = MLX5_IB_OPCODE(MLX5_OPCODE_RECV_ENABLE,         MLX5_OPCODE_MANAGED, 0),
	[IBV_EXP_WR_CQE_WAIT]                   = MLX5_IB_OPCODE(MLX5_OPCODE_CQE_WAIT,            MLX5_OPCODE_MANAGED, 0),
	[IBV_EXP_WR_NOP]			= MLX5_IB_OPCODE(MLX5_OPCODE_NOP,		  MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_UMR_FILL]			= MLX5_IB_OPCODE(MLX5_OPCODE_UMR,		  MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_UMR_INVALIDATE]             = MLX5_IB_OPCODE(MLX5_OPCODE_UMR,                 MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_TSO]			= MLX5_IB_OPCODE(MLX5_OPCODE_TSO,                 MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_BIND_MW]			= MLX5_IB_OPCODE(MLX5_OPCODE_UMR,                 MLX5_OPCODE_BASIC, 0),
	[IBV_EXP_WR_LOCAL_INV]			= MLX5_IB_OPCODE(MLX5_OPCODE_UMR,                 MLX5_OPCODE_BASIC, 0),
};

enum {
	MLX5_CALC_UINT64_ADD    = 0x01,
	MLX5_CALC_FLOAT64_ADD   = 0x02,
	MLX5_CALC_UINT64_MAXLOC = 0x03,
	MLX5_CALC_UINT64_AND    = 0x04,
	MLX5_CALC_UINT64_OR     = 0x05,
	MLX5_CALC_UINT64_XOR    = 0x06
};

enum {
	MLX5_TM_OPCODE_NOP		= 0x00,
	MLX5_TM_OPCODE_APPEND		= 0x01,
	MLX5_TM_OPCODE_REMOVE		= 0x02
};

static const struct mlx5_calc_op {
	int valid;
	uint8_t opmod;
}  mlx5_calc_ops_table
	[IBV_EXP_CALC_DATA_SIZE_NUMBER]
		[IBV_EXP_CALC_OP_NUMBER]
			[IBV_EXP_CALC_DATA_TYPE_NUMBER] = {
	[IBV_EXP_CALC_DATA_SIZE_64_BIT] = {
		[IBV_EXP_CALC_OP_ADD] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_ADD },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_ADD },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opmod = MLX5_CALC_FLOAT64_ADD }
		},
		[IBV_EXP_CALC_OP_BXOR] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_XOR },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_XOR },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_XOR }
		},
		[IBV_EXP_CALC_OP_BAND] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_AND },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_AND },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_AND }
		},
		[IBV_EXP_CALC_OP_BOR] = {
			[IBV_EXP_CALC_DATA_TYPE_INT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_OR },
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_OR },
			[IBV_EXP_CALC_DATA_TYPE_FLOAT]  = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_OR }
		},
		[IBV_EXP_CALC_OP_MAXLOC] = {
			[IBV_EXP_CALC_DATA_TYPE_UINT] = {
				.valid = 1,
				.opmod = MLX5_CALC_UINT64_MAXLOC }
		}
	}
};

static inline void set_wait_en_seg(void *wqe_seg, uint32_t obj_num, uint32_t count)
{
	struct mlx5_wqe_wait_en_seg *seg = (struct mlx5_wqe_wait_en_seg *)wqe_seg;

	seg->pi      = htonl(count);
	seg->obj_num = htonl(obj_num);

	return;
}

static inline void *get_recv_wqe(struct mlx5_wq *rq, int n)
{
	return rq->buff + (n << rq->wqe_shift);
}

static int copy_to_scat(struct mlx5_wqe_data_seg *scat, void *buf, int *size,
			 int max, int convert2host_endianness)
{
	int copy;
	int i;
	void *buf2cpy = buf;

	if (unlikely(!(*size)))
		return IBV_WC_SUCCESS;

	for (i = 0; i < max; ++i) {
#if (__BYTE_ORDER != __BIG_ENDIAN)
		union {
			uint64_t buf64;
			uint32_t buf32;
			uint16_t buf16;
		} tmp;
#endif

		copy = min(*size, ntohl(scat->byte_count));

#if (__BYTE_ORDER != __BIG_ENDIAN)
		if (convert2host_endianness) {
			buf2cpy = (void *)&tmp;

			if (copy == 8) {
				tmp.buf64 = (uint64_t)ntohll(*((uint64_t *)buf));
			} else if (copy == 4) {
				tmp.buf32 = (uint32_t)ntohl(*((uint32_t *)buf));
			} else if (copy == 2) {
				tmp.buf16 = (uint16_t)ntohs(*((uint16_t *)buf));
			} else {
				fprintf(stderr, "doesn't support %dB to convert endianness\n", copy);
				buf2cpy = buf;
			}
		}
#endif

		memcpy((void *)(unsigned long)ntohll(scat->addr), buf2cpy, copy);

		*size -= copy;
		if (*size == 0)
			return IBV_WC_SUCCESS;

		buf += copy;
		++scat;
	}
	return IBV_WC_LOC_LEN_ERR;
}

int mlx5_copy_to_recv_wqe(struct mlx5_qp *qp, int idx, void *buf, int size)
{
	struct mlx5_wqe_data_seg *scat;
	int max = 1 << (qp->rq.wqe_shift - 4);

	scat = get_recv_wqe(&qp->rq, idx);
	if (unlikely(qp->ctrl_seg.wq_sig))
		++scat;

	return copy_to_scat(scat, buf, &size, max, 0);
}

int mlx5_copy_to_send_wqe(struct mlx5_qp *qp, int idx, void *buf, int size)
{
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct mlx5_wqe_data_seg *scat;
	void *p;
	int max;
	unsigned opmod_idx_opcode;
	unsigned op;
	unsigned arg_length_log;
	int convert2host_endianness = 0;
#if (__BYTE_ORDER != __BIG_ENDIAN)
	struct mlx5_context *mctx;
#endif

	idx &= (qp->sq.wqe_cnt - 1);
	ctrl = mlx5_get_send_wqe(qp, idx);
	if (qp->verbs_qp.qp.qp_type != IBV_QPT_RC && qp->verbs_qp.qp.qp_type != IBV_EXP_QPT_DC_INI) {
		fprintf(stderr, "scatter to CQE is supported only for RC or DC QPs\n");
		return IBV_WC_GENERAL_ERR;
	}
	p = ctrl + 1;

	if (IBV_EXP_QPT_DC_INI == qp->verbs_qp.qp.qp_type) {
		struct mlx5_wqe_datagram_seg *dc_seg = p;
		/* DC transport segment can be extended with an AV */
		p += dc_seg->av.base.dqp_dct & ntohl(MLX5_EXTENDED_UD_AV) ?
			sizeof(dc_seg->av) : sizeof(dc_seg->av.base);
	}

	opmod_idx_opcode = ntohl(ctrl->opmod_idx_opcode);
	op = opmod_idx_opcode & 0xff;
	switch (op) {
	case MLX5_OPCODE_RDMA_READ:
		p = p + sizeof(struct mlx5_wqe_raddr_seg);
		break;

	case MLX5_OPCODE_ATOMIC_MASKED_CS:
	case MLX5_OPCODE_ATOMIC_MASKED_FA:
		p = p + sizeof(struct mlx5_wqe_raddr_seg);
		arg_length_log = (opmod_idx_opcode >> 24) & 0x7;
		if (likely(arg_length_log < 7)) {
			unsigned atomic_arg_length = 1 << (arg_length_log + 2);

			if (op == MLX5_OPCODE_ATOMIC_MASKED_FA)
				p += align(atomic_arg_length << 1, 16);
			else
				p += (atomic_arg_length << 2);
		} else { /* Indirect reference (arg_length_log == 7) */
			p += sizeof(struct mlx5_wqe_data_seg);
		}

#if (__BYTE_ORDER != __BIG_ENDIAN)
		mctx = to_mctx(qp->verbs_qp.qp.context);
		convert2host_endianness = !(mctx->info.masked_log_atomic_arg_sizes_network_endianness &
					  (uint64_t)size);
#endif
		break;
	case MLX5_OPCODE_ATOMIC_CS:
	case MLX5_OPCODE_ATOMIC_FA:
		p = p + sizeof(struct mlx5_wqe_raddr_seg);
		p = p + sizeof(struct mlx5_wqe_atomic_seg);

#if (__BYTE_ORDER != __BIG_ENDIAN)
		mctx = to_mctx(qp->verbs_qp.qp.context);
		convert2host_endianness = (mctx->info.exp_atomic_cap == IBV_EXP_ATOMIC_HCA);
#endif
		break;

	default:
		fprintf(stderr, "scatter to CQE for opcode %d\n", op);
		return IBV_WC_REM_INV_REQ_ERR;
	}

	scat = p;
	max = (ntohl(ctrl->qpn_ds) & 0x3F) - (((void *)scat - (void *)ctrl) >> 4);
	if (unlikely((void *)(scat + max) > qp->gen_data.sqend)) {
		unsigned int scat_offset = 0;

		if ((void *)scat < qp->gen_data.sqend) {
			int tmp = (qp->gen_data.sqend - (void *)scat) >> 4;
			int orig_size = size;

			if (copy_to_scat(scat, buf, &size, tmp,
				convert2host_endianness) == IBV_WC_SUCCESS)
				return IBV_WC_SUCCESS;

			max = max - tmp;
			buf += orig_size - size;
		} else {
			scat_offset = (void *)scat - qp->gen_data.sqend;
		}

		scat = mlx5_get_send_wqe(qp, 0) + scat_offset;
	}

	return copy_to_scat(scat, buf, &size, max, convert2host_endianness);
}

void mlx5_init_qp_indices(struct mlx5_qp *qp)
{
	qp->sq.head	 = 0;
	qp->sq.tail	 = 0;
	qp->rq.head	 = 0;
	qp->rq.tail	 = 0;
	qp->gen_data.scur_post = 0;
	qp->sq_enable.head_en_index = 0;
	qp->sq_enable.head_en_count = 0;
	qp->rq_enable.head_en_index = 0;
	qp->rq_enable.head_en_count = 0;
	qp->peer_scur_post = 0;
	qp->peer_ctrl_seg  = NULL;
}

void mlx5_init_rwq_indices(struct mlx5_rwq *rwq)
{
	rwq->rq.head	 = 0;
	rwq->rq.tail	 = 0;
	rwq->rq_enable.head_en_index = 0;
	rwq->rq_enable.head_en_count = 0;
}

static int __mlx5_wq_overflow(int is_rq, struct mlx5_wq *wq, int nreq, struct mlx5_qp *qp) __attribute__((noinline));
static int __mlx5_wq_overflow(int is_rq, struct mlx5_wq *wq, int nreq, struct mlx5_qp *qp)
{
	struct mlx5_cq *cq = to_mcq(is_rq ? qp->verbs_qp.qp.recv_cq : qp->verbs_qp.qp.send_cq);
	unsigned cur;


	mlx5_lock(&cq->lock);
	cur = wq->head - wq->tail;
	mlx5_unlock(&cq->lock);

	return cur + nreq >= wq->max_post;
}
static inline int mlx5_wq_overflow(int is_rq, int nreq, struct mlx5_qp *qp) __attribute__((always_inline));
static inline int mlx5_wq_overflow(int is_rq, int nreq, struct mlx5_qp *qp)
{
	unsigned cur;
	struct mlx5_wq *wq = is_rq ? &qp->rq : &qp->sq;

	cur = wq->head - wq->tail;
	if (likely(cur + nreq < wq->max_post))
		return 0;

	return __mlx5_wq_overflow(is_rq, wq, nreq, qp);
}

static inline void set_raddr_seg(struct mlx5_wqe_raddr_seg *rseg,
				 uint64_t remote_addr, uint32_t rkey)
{
	rseg->raddr    = htonll(remote_addr);
	rseg->rkey     = htonl(rkey);
	rseg->reserved = 0;
}

static void set_tm_seg(struct mlx5_wqe_tm_seg *tmseg, int op,
		       struct ibv_exp_ops_wr *wr, int index)
{
	tmseg->flags = 0;
	if (wr->flags & IBV_EXP_OPS_SIGNALED)
		tmseg->flags |= MLX5_SRQ_FLAG_TM_CQE_REQ;
	if (wr->flags & IBV_EXP_OPS_TM_SYNC) {
		tmseg->flags |= MLX5_SRQ_FLAG_TM_SW_CNT;
		tmseg->sw_cnt = htons(wr->tm.unexpected_cnt);
	}
	tmseg->opcode = op << 4;
	if (op == MLX5_TM_OPCODE_NOP)
		return;
	tmseg->index = htons(index);
	if (op == MLX5_TM_OPCODE_REMOVE)
		return;
	tmseg->append_tag = htonll(wr->tm.add.tag);
	tmseg->append_mask = htonll(wr->tm.add.mask);
}

static void set_atomic_seg(struct mlx5_wqe_atomic_seg *aseg,
			   enum ibv_wr_opcode   opcode,
			   uint64_t swap,
			   uint64_t compare_add)
{
	if (opcode == IBV_WR_ATOMIC_CMP_AND_SWP) {
		aseg->swap_add = htonll(swap);
		aseg->compare  = htonll(compare_add);
	} else {
		aseg->swap_add = htonll(compare_add);
		aseg->compare  = 0;
	}
}

static int has_grh(struct mlx5_ah *ah)
{
	return ah->av.base.dqp_dct & ntohl(MLX5_EXTENDED_UD_AV);
}

static int set_ipoib_datagram_seg(struct mlx5_wqe_datagram_seg *dseg,
				  struct ibv_exp_send_wr *wr)
{
	struct mlx5_ah *ah = to_mah(wr->wr.ud.ah);
	int size;

	size = sizeof(ah->av);

	if (has_grh(ah)) {
		memcpy(&dseg->av, &to_mah(wr->wr.ud.ah)->av, size);
	} else {
		memcpy(&dseg->av, &to_mah(wr->wr.ud.ah)->av, sizeof(ah->av.base));
		dseg->av.base.dqp_dct |= htonl(MLX5_EXTENDED_UD_AV);
		dseg->av.grh_sec.grh_gid_fl = 0; /* no GRH */
	}

	dseg->av.base.dqp_dct |= htonl(wr->wr.ud.remote_qpn);
	dseg->av.base.key.qkey.qkey = htonl(wr->wr.ud.remote_qkey);

	return size;
}

static int set_datagram_seg(struct mlx5_wqe_datagram_seg *dseg,
			    struct ibv_exp_send_wr *wr)
{
	struct mlx5_ah *ah = to_mah(wr->wr.ud.ah);
	int size;

	size = has_grh(ah) ? sizeof(ah->av) : sizeof(ah->av.base);

	memcpy(&dseg->av, &to_mah(wr->wr.ud.ah)->av, size);
	dseg->av.base.dqp_dct |= htonl(wr->wr.ud.remote_qpn);
	dseg->av.base.key.qkey.qkey = htonl(wr->wr.ud.remote_qkey);

	return size;
}

static int set_dci_seg(struct mlx5_wqe_datagram_seg *dseg,
		       struct ibv_exp_send_wr *wr)
{
	struct mlx5_ah *ah = to_mah(wr->dc.ah);
	int size;

	size = has_grh(ah) ? sizeof(ah->av) : sizeof(ah->av.base);

	memcpy(&dseg->av, &to_mah(wr->dc.ah)->av, size);
	dseg->av.base.dqp_dct |= htonl(wr->dc.dct_number);
	dseg->av.base.key.dc_key = htonll(wr->dc.dct_access_key);

	return size;
}

static int set_odp_data_ptr_seg(struct mlx5_wqe_data_seg *dseg, struct ibv_sge *sg,
				struct mlx5_qp *qp) __attribute__((noinline));
static int set_odp_data_ptr_seg(struct mlx5_wqe_data_seg *dseg, struct ibv_sge *sg,
				struct mlx5_qp *qp)
{
	uint32_t lkey;
	if (sg->lkey == ODP_GLOBAL_R_LKEY) {
		if (mlx5_get_real_lkey_from_implicit_lkey(qp->odp_data.pd, &qp->odp_data.pd->r_ilkey,
							  sg->addr, sg->length,
							  &lkey))
			return ENOMEM;
	} else {
		if (mlx5_get_real_lkey_from_implicit_lkey(qp->odp_data.pd, &qp->odp_data.pd->w_ilkey,
							  sg->addr, sg->length,
							  &lkey))
			return ENOMEM;
	}

	dseg->byte_count = htonl(sg->length);
	dseg->lkey       = htonl(lkey);
	dseg->addr       = htonll(sg->addr);

	return 0;
}

static inline int set_data_ptr_seg(struct mlx5_wqe_data_seg *dseg, struct ibv_sge *sg,
			    struct mlx5_qp *qp,
			    int offset) __attribute__((always_inline));
static inline int set_data_ptr_seg(struct mlx5_wqe_data_seg *dseg, struct ibv_sge *sg,
			    struct mlx5_qp *qp,
			    int offset)
{
	if (unlikely(sg->lkey == ODP_GLOBAL_R_LKEY || sg->lkey == ODP_GLOBAL_W_LKEY))
		return set_odp_data_ptr_seg(dseg, sg, qp);

	dseg->byte_count = htonl(sg->length - offset);
	dseg->lkey       = htonl(sg->lkey);
	dseg->addr       = htonll(sg->addr + offset);

	return 0;
}

static uint32_t send_ieth(struct ibv_exp_send_wr *wr)
{
	return MLX5_IB_OPCODE_GET_ATTR(mlx5_ib_opcode[wr->exp_opcode]) &
			MLX5_OPCODE_WITH_IMM ?
				wr->ex.imm_data : 0;
}

static inline int set_data_inl_seg(struct mlx5_qp *qp, int num_sge, struct ibv_sge *sg_list,
		     void *wqe, int *sz,
		     int idx, int offset) __attribute__((always_inline));
static inline int set_data_inl_seg(struct mlx5_qp *qp, int num_sge, struct ibv_sge *sg_list,
		     void *wqe, int *sz, int idx, int offset)
{
	struct mlx5_wqe_inline_seg *seg;
	void *addr;
	int len;
	int i;
	int inl = 0;
	void *qend = qp->gen_data.sqend;
	int copy;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif

	seg = wqe;
	wqe += sizeof *seg;

	for (i = idx; i < num_sge; ++i) {
		addr = (void *) (unsigned long)(sg_list[i].addr + offset);
		len  = sg_list[i].length - offset;
		inl += len;
		offset = 0;

		if (unlikely(inl > qp->data_seg.max_inline_data)) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "inline layout failed, err %d\n", ENOMEM);
			return ENOMEM;
		}

		if (unlikely(wqe + len > qend)) {
			copy = qend - wqe;
			memcpy(wqe, addr, copy);
			addr += copy;
			len -= copy;
			wqe = mlx5_get_send_wqe(qp, 0);
		}
		memcpy(wqe, addr, len);
		wqe += len;
	}

	if (likely(inl)) {
		seg->byte_count = htonl(inl | MLX5_INLINE_SEG);
		*sz += align(inl + sizeof(seg->byte_count), 16) / 16;
	}

	return 0;
}

static inline int set_data_non_inl_seg(struct mlx5_qp *qp, int num_sge, struct ibv_sge *sg_list,
			 void *wqe, int *sz,
			 int idx, int offset, int is_tso) __attribute__((always_inline));
static inline int set_data_non_inl_seg(struct mlx5_qp *qp, int num_sge, struct ibv_sge *sg_list,
			 void *wqe, int *sz,
			 int idx, int offset, int is_tso)
{
	struct mlx5_context *ctx = to_mctx(qp->verbs_qp.qp.context);
	struct mlx5_wqe_data_seg *dpseg = wqe;
	struct ibv_sge *psge;
	int i;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif
	uint32_t max_tso = ctx->max_tso;

	for (i = idx; i < num_sge; ++i) {
		if (unlikely(is_tso)) {
			if (unlikely(max_tso < sg_list[i].length)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND,
					 "max tso payload length is %d\n",
					 ctx->max_tso);
				return EINVAL;
			}
			max_tso -= sg_list[i].length;
		}

		if (unlikely(dpseg == qp->gen_data.sqend))
			dpseg = mlx5_get_send_wqe(qp, 0);

		if (likely(sg_list[i].length)) {
			psge = sg_list + i;

			if (unlikely(set_data_ptr_seg(dpseg, psge, qp,
						      offset))) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "failed allocating memory for implicit lkey structure\n");
				return ENOMEM;
			}
			++dpseg;
			offset = 0;
			*sz += sizeof(struct mlx5_wqe_data_seg) / 16;
		}
	}

	return 0;
}

static int set_data_atom_seg(struct mlx5_qp *qp, int num_sge, struct ibv_sge *sg_list,
			     void *wqe, int *sz, int atom_arg) __MLX5_ALGN_F__;
static int set_data_atom_seg(struct mlx5_qp *qp, int num_sge, struct ibv_sge *sg_list,
			     void *wqe, int *sz, int atom_arg)
{
	struct mlx5_wqe_data_seg *dpseg = wqe;
	struct ibv_sge *psge;
	struct ibv_sge sge;
	int i;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif

	for (i = 0; i < num_sge; ++i) {
		if (unlikely(dpseg == qp->gen_data.sqend))
			dpseg = mlx5_get_send_wqe(qp, 0);

		if (likely(sg_list[i].length)) {
			sge = sg_list[i];
			sge.length = atom_arg;
			psge = &sge;
			if (unlikely(set_data_ptr_seg(dpseg, psge, qp, 0))) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "failed allocating memory for implicit lkey structure\n");
				return ENOMEM;
			}
			++dpseg;
			*sz += sizeof(struct mlx5_wqe_data_seg) / 16;
		}
	}

	return 0;
}

static inline int set_data_seg(struct mlx5_qp *qp, void *seg, int *sz, int is_inl,
		 int num_sge, struct ibv_sge *sg_list, int atom_arg,
		 int idx, int offset, int is_tso) __attribute__((always_inline));
static inline int set_data_seg(struct mlx5_qp *qp, void *seg, int *sz, int is_inl,
		 int num_sge, struct ibv_sge *sg_list, int atom_arg,
		 int idx, int offset, int is_tso)
{
	if (is_inl)
		return set_data_inl_seg(qp, num_sge, sg_list, seg, sz, idx,
					offset);
	if (unlikely(atom_arg))
		return set_data_atom_seg(qp, num_sge, sg_list, seg, sz, atom_arg);

	return set_data_non_inl_seg(qp, num_sge, sg_list, seg, sz, idx, offset, is_tso);
}

#ifdef MLX5_DEBUG
void dump_wqe(FILE *fp, int idx, int size_16, struct mlx5_qp *qp)
{
	uint32_t *uninitialized_var(p);
	int i, j;
	int tidx = idx;

	fprintf(fp, "dump wqe at %p\n", mlx5_get_send_wqe(qp, tidx));
	for (i = 0, j = 0; i < size_16 * 4; i += 4, j += 4) {
		if ((i & 0xf) == 0) {
			void *buf = mlx5_get_send_wqe(qp, tidx);
			tidx = (tidx + 1) & (qp->sq.wqe_cnt - 1);
			p = buf;
			j = 0;
		}
		fprintf(fp, "%08x %08x %08x %08x\n", ntohl(p[j]), ntohl(p[j + 1]),
			ntohl(p[j + 2]), ntohl(p[j + 3]));
	}
}
#endif /* MLX5_DEBUG */


void *mlx5_get_atomic_laddr(struct mlx5_qp *qp, uint16_t idx, int *byte_count)
{
	struct mlx5_wqe_data_seg *dpseg;
	void *addr;

	dpseg = mlx5_get_send_wqe(qp, idx) + sizeof(struct mlx5_wqe_ctrl_seg) +
		sizeof(struct mlx5_wqe_raddr_seg) +
		sizeof(struct mlx5_wqe_atomic_seg);
	addr = (void *)(unsigned long)ntohll(dpseg->addr);

	/*
	 * Currently byte count is always 8 bytes. Fix this when
	 * we support variable size of atomics
	 */
	*byte_count = 8;
	return addr;
}

static int ext_cmp_swp(struct mlx5_qp *qp, void *seg,
		       struct ibv_exp_send_wr *wr)
{
	struct ibv_exp_cmp_swap *cs = &wr->ext_op.masked_atomics.wr_data.inline_data.op.cmp_swap;
	int arg_sz = 1 << wr->ext_op.masked_atomics.log_arg_sz;
	uint32_t *p32 = seg;
	uint64_t *p64 = seg;
	int i;

	if (arg_sz == 4) {
		*p32 = htonl((uint32_t)cs->swap_val);
		p32++;
		*p32 = htonl((uint32_t)cs->compare_val);
		p32++;
		*p32 = htonl((uint32_t)cs->swap_mask);
		p32++;
		*p32 = htonl((uint32_t)cs->compare_mask);
		return 16;
	} else if (arg_sz == 8) {
		*p64 = htonll(cs->swap_val);
		p64++;
		*p64 = htonll(cs->compare_val);
		p64++;
		if (unlikely(p64 == qp->gen_data.sqend))
			p64 = mlx5_get_send_wqe(qp, 0);
		*p64 = htonll(cs->swap_mask);
		p64++;
		*p64 = htonll(cs->compare_mask);
		return 32;
	} else {
		for (i = 0; i < arg_sz; i += 8, p64++) {
			if (unlikely(p64 == qp->gen_data.sqend))
				p64 = mlx5_get_send_wqe(qp, 0);
			*p64 = htonll(*(uint64_t *)(uintptr_t)(cs->swap_val + i));
		}

		for (i = 0; i < arg_sz; i += 8, p64++) {
			if (unlikely(p64 == qp->gen_data.sqend))
				p64 = mlx5_get_send_wqe(qp, 0);
			*p64 = htonll(*(uint64_t *)(uintptr_t)(cs->compare_val + i));
		}

		for (i = 0; i < arg_sz; i += 8, p64++) {
			if (unlikely(p64 == qp->gen_data.sqend))
				p64 = mlx5_get_send_wqe(qp, 0);
			*p64 = htonll(*(uint64_t *)(uintptr_t)(cs->swap_mask + i));
		}

		for (i = 0; i < arg_sz; i += 8, p64++) {
			if (unlikely(p64 == qp->gen_data.sqend))
				p64 = mlx5_get_send_wqe(qp, 0);
			*p64 = htonll(*(uint64_t *)(uintptr_t)(cs->compare_mask + i));
		}
		return 4 * arg_sz;
	}
}

static int ext_fetch_add(struct mlx5_qp *qp, void *seg,
			 struct ibv_exp_send_wr *wr)
{
	struct ibv_exp_fetch_add *fa = &wr->ext_op.masked_atomics.wr_data.inline_data.op.fetch_add;
	int arg_sz = 1 << wr->ext_op.masked_atomics.log_arg_sz;
	uint32_t *p32 = seg;
	uint64_t *p64 = seg;
	int i;

	if (arg_sz == 4) {
		*p32 = htonl((uint32_t)fa->add_val);
		p32++;
		*p32 = htonl((uint32_t)fa->field_boundary);
		p32++;
		*p32 = htonl(0);
		p32++;
		*p32 = htonl(0);
		return 16;
	} else if (arg_sz == 8) {
		*p64 = htonll(fa->add_val);
		p64++;
		*p64 = htonll(fa->field_boundary);
		return 16;
	} else {
		for (i = 0; i < arg_sz; i += 8, p64++) {
			if (unlikely(p64 == qp->gen_data.sqend))
				p64 = mlx5_get_send_wqe(qp, 0);
			*p64 = htonll(*(uint64_t *)(uintptr_t)(fa->add_val + i));
		}

		for (i = 0; i < arg_sz; i += 8, p64++) {
			if (unlikely(p64 == qp->gen_data.sqend))
				p64 = mlx5_get_send_wqe(qp, 0);
			*p64 = htonll(*(uint64_t *)(uintptr_t)(fa->field_boundary + i));
		}

		return 2 * arg_sz;
	}
}

static int set_ext_atomic_seg(struct mlx5_qp *qp, void *seg,
			      struct ibv_exp_send_wr *wr)
{
	/* currently only inline is supported */
	if (unlikely(!(wr->exp_send_flags & IBV_EXP_SEND_EXT_ATOMIC_INLINE)))
		return -1;

	if (unlikely((1 << wr->ext_op.masked_atomics.log_arg_sz) > qp->max_atomic_arg))
		return -1;

	if (wr->exp_opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP)
		return ext_cmp_swp(qp, seg, wr);
	else if (wr->exp_opcode == IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD)
		return ext_fetch_add(qp, seg, wr);
	else
		return -1;
}

static uint64_t umr_mask(int fill)
{
	uint64_t mask;

	if (fill)
		mask =  MLX5_MKEY_MASK_LEN		|
			MLX5_MKEY_MASK_START_ADDR	|
			MLX5_MKEY_MASK_LR		|
			MLX5_MKEY_MASK_LW		|
			MLX5_MKEY_MASK_RR		|
			MLX5_MKEY_MASK_RW		|
			MLX5_MKEY_MASK_FREE		|
			MLX5_MKEY_MASK_A;
	else
		mask = MLX5_MKEY_MASK_FREE;

	return mask;
}

static void set_umr_ctrl_seg(struct ibv_exp_send_wr *wr,
			     struct mlx5_wqe_umr_ctrl_seg *seg)
{
	int fill = wr->exp_opcode == IBV_EXP_WR_UMR_FILL ? 1 : 0;

	memset(seg, 0, sizeof(*seg));

	if (wr->exp_send_flags & IBV_EXP_SEND_INLINE || !fill)
		seg->flags = MLX5_UMR_CTRL_INLINE;

	seg->mkey_mask = htonll(umr_mask(fill));
}

enum {
	MLX5_KLMS_IN_LIST = 4
};

static int lay_umr(struct mlx5_qp *qp, struct ibv_exp_send_wr *wr,
		   void *seg, int *wqe_size, int *xlat_size,
		   uint64_t *reglen)
{
	enum ibv_exp_umr_wr_type type = wr->ext_op.umr.umr_type;
	struct ibv_exp_mem_region *mlist;
	struct ibv_exp_mem_repeat_block *rep;
	struct mlx5_wqe_data_seg *dseg;
	struct mlx5_seg_repeat_block *rb;
	struct mlx5_seg_repeat_ent *re;
	struct mlx5_klm_buf *klm = NULL;
	void *qend = qp->gen_data.sqend;
	int i;
	int j;
	int n;
	int byte_count = 0;
	int inl = wr->exp_send_flags & IBV_EXP_SEND_INLINE;
	void *buf;
	int tmp;
	int pad;

	if (inl) {
		if (unlikely(qp->max_inl_send_klms <
			     wr->ext_op.umr.num_mrs))
			return EINVAL;
		buf = seg;
	} else {
		klm = to_klm(wr->ext_op.umr.memory_objects);
		buf = klm->buf;
	}

	*reglen = 0;
	n = wr->ext_op.umr.num_mrs;
	if (type == IBV_EXP_UMR_MR_LIST) {
		mlist = wr->ext_op.umr.mem_list.mem_reg_list;
		dseg = buf;

		pad = (n % MLX5_KLMS_IN_LIST)
		      ? n + (MLX5_KLMS_IN_LIST - (n % MLX5_KLMS_IN_LIST))
		      : n;
		for (i = 0, j = 0; i < pad; i++, j++) {
			if (inl && unlikely((&dseg[j] == qend))) {
				dseg = mlx5_get_send_wqe(qp, 0);
				j = 0;
			}
			if (likely(i < n)) {
				dseg[j].addr =  htonll((uint64_t)(uintptr_t)mlist[i].base_addr);
				dseg[j].lkey = htonl(mlist[i].mr->lkey);
				dseg[j].byte_count = htonl(mlist[i].length);
				byte_count += mlist[i].length;
			} else if (inl) {
				/*
				 * KLM list is aligned to 64B (16B per KLM).
				 * According to the PRM, remaining padding must
				 * be set to 0.
				 */
				dseg[j].addr = 0;
				dseg[j].lkey = 0;
				dseg[j].byte_count = 0;
			} else {
				break;
			}
		}
		if (inl)
			*wqe_size = align(n * sizeof(*dseg), 64);
		else
			*wqe_size = 0;

		*reglen = byte_count;
		*xlat_size = n * sizeof(*dseg);
	} else {
		rep = wr->ext_op.umr.mem_list.rb.mem_repeat_block_list;
		rb = buf;
		rb->const_0x400 = htonl(0x400);
		rb->reserved = 0;
		rb->num_ent = htons(n);
		re = rb->entries;
		rb->repeat_count = htonl(wr->ext_op.umr.mem_list.rb.repeat_count[0]);

		if (unlikely(wr->ext_op.umr.mem_list.rb.stride_dim != 1)) {
			fprintf(stderr, "dimention must be 1\n");
			return -ENOMEM;
		}


		for (i = 0, j = 0; i < n; i++, j++, rep++, re++) {
			if (inl && unlikely((re == qend)))
				re = mlx5_get_send_wqe(qp, 0);

			byte_count += rep->byte_count[0];
			re->va = htonll(rep->base_addr);
			re->byte_count = htons(rep->byte_count[0]);
			re->stride = htons(rep->stride[0]);
			re->memkey = htonl(rep->mr->lkey);
		}
		rb->byte_count = htonl(byte_count);
		*reglen = byte_count * ntohl(rb->repeat_count);
		tmp = align((n + 1), 4) - n - 1;
		memset(re, 0, tmp * sizeof(*re));
		if (inl) {
			*wqe_size = align(sizeof(*rb) + sizeof(*re) * n, 64);
			*xlat_size = (n + 1) * sizeof(*re);
		} else {
			*wqe_size = 0;
			*xlat_size = (n + 1) * sizeof(*re);
		}
	}
	return 0;
}

static void *adjust_seg(struct mlx5_qp *qp, void *seg)
{
	return mlx5_get_send_wqe(qp, 0) + (seg - qp->gen_data.sqend);
}

static uint8_t get_umr_flags(int acc)
{
	return (acc & IBV_ACCESS_REMOTE_ATOMIC ? MLX5_PERM_ATOMIC       : 0) |
	       (acc & IBV_ACCESS_REMOTE_WRITE  ? MLX5_PERM_REMOTE_WRITE : 0) |
	       (acc & IBV_ACCESS_REMOTE_READ   ? MLX5_PERM_REMOTE_READ  : 0) |
	       (acc & IBV_ACCESS_LOCAL_WRITE   ? MLX5_PERM_LOCAL_WRITE  : 0) |
		MLX5_PERM_LOCAL_READ | MLX5_PERM_UMR_EN;
}

static void set_mkey_seg(struct ibv_exp_send_wr *wr, struct mlx5_mkey_seg *seg)
{
	memset(seg, 0, sizeof(*seg));
	if (wr->exp_opcode != IBV_EXP_WR_UMR_FILL) {
		seg->status = 1 << 6;
		return;
	}

	seg->flags = get_umr_flags(wr->ext_op.umr.exp_access);
	seg->start_addr = htonll(wr->ext_op.umr.base_addr);
	seg->qpn_mkey7_0 = htonl(0xffffff00 | (wr->ext_op.umr.modified_mr->lkey & 0xff));
}

static uint8_t get_fence(uint8_t fence, struct ibv_exp_send_wr *wr)
{
	if (unlikely(wr->exp_opcode == IBV_EXP_WR_LOCAL_INV &&
		     wr->exp_send_flags & IBV_EXP_SEND_FENCE))
		return MLX5_FENCE_MODE_STRONG_ORDERING;

	if (unlikely(fence)) {
		if (wr->exp_send_flags & IBV_EXP_SEND_FENCE)
			return MLX5_FENCE_MODE_SMALL_AND_FENCE;
		else
			return fence;

	} else {
		return 0;
	}
}

void mlx5_build_ctrl_seg_data(struct mlx5_qp *qp, uint32_t qp_num)
{
	uint8_t *tbl = qp->ctrl_seg.fm_ce_se_tbl;
	uint8_t *acc = qp->ctrl_seg.fm_ce_se_acc;
	int i;

	tbl[0		       | 0		   | 0]		     = (0			| 0			  | 0);
	tbl[0		       | 0		   | IBV_SEND_FENCE] = (0			| 0			  | MLX5_WQE_CTRL_FENCE);
	tbl[0		       | IBV_SEND_SIGNALED | 0]		     = (0			| MLX5_WQE_CTRL_CQ_UPDATE | 0);
	tbl[0		       | IBV_SEND_SIGNALED | IBV_SEND_FENCE] = (0			| MLX5_WQE_CTRL_CQ_UPDATE | MLX5_WQE_CTRL_FENCE);
	tbl[IBV_SEND_SOLICITED | 0		   | 0]		     = (MLX5_WQE_CTRL_SOLICITED | 0			  | 0);
	tbl[IBV_SEND_SOLICITED | 0		   | IBV_SEND_FENCE] = (MLX5_WQE_CTRL_SOLICITED | 0			  | MLX5_WQE_CTRL_FENCE);
	tbl[IBV_SEND_SOLICITED | IBV_SEND_SIGNALED | 0]		     = (MLX5_WQE_CTRL_SOLICITED | MLX5_WQE_CTRL_CQ_UPDATE | 0);
	tbl[IBV_SEND_SOLICITED | IBV_SEND_SIGNALED | IBV_SEND_FENCE] = (MLX5_WQE_CTRL_SOLICITED | MLX5_WQE_CTRL_CQ_UPDATE | MLX5_WQE_CTRL_FENCE);
	for (i = 0; i < 8; i++)
		tbl[i] = qp->sq_signal_bits | tbl[i];

	memset(acc, 0, sizeof(qp->ctrl_seg.fm_ce_se_acc));
	acc[0			       | 0			   | 0]			     = (0			| 0			  | 0);
	acc[0			       | 0			   | IBV_EXP_QP_BURST_FENCE] = (0			| 0			  | MLX5_WQE_CTRL_FENCE);
	acc[0			       | IBV_EXP_QP_BURST_SIGNALED | 0]			     = (0			| MLX5_WQE_CTRL_CQ_UPDATE | 0);
	acc[0			       | IBV_EXP_QP_BURST_SIGNALED | IBV_EXP_QP_BURST_FENCE] = (0			| MLX5_WQE_CTRL_CQ_UPDATE | MLX5_WQE_CTRL_FENCE);
	acc[IBV_EXP_QP_BURST_SOLICITED | 0			   | 0]			     = (MLX5_WQE_CTRL_SOLICITED | 0			  | 0);
	acc[IBV_EXP_QP_BURST_SOLICITED | 0			   | IBV_EXP_QP_BURST_FENCE] = (MLX5_WQE_CTRL_SOLICITED | 0			  | MLX5_WQE_CTRL_FENCE);
	acc[IBV_EXP_QP_BURST_SOLICITED | IBV_EXP_QP_BURST_SIGNALED | 0]			     = (MLX5_WQE_CTRL_SOLICITED | MLX5_WQE_CTRL_CQ_UPDATE | 0);
	acc[IBV_EXP_QP_BURST_SOLICITED | IBV_EXP_QP_BURST_SIGNALED | IBV_EXP_QP_BURST_FENCE] = (MLX5_WQE_CTRL_SOLICITED | MLX5_WQE_CTRL_CQ_UPDATE | MLX5_WQE_CTRL_FENCE);
	for (i = 0; i < 32; i++)
		acc[i] = qp->sq_signal_bits | acc[i];

	qp->ctrl_seg.qp_num = qp_num;
}

static inline void set_ctrl_seg_sig(uint32_t *start, struct ctrl_seg_data *ctrl_seg,
				    uint8_t opcode, uint16_t idx, uint8_t opmod,
				    uint8_t size, uint8_t fm_ce_se, uint32_t imm_invk_umrk)
{
	set_ctrl_seg(start, ctrl_seg, opcode, idx, opmod, size, fm_ce_se, imm_invk_umrk);

	if (unlikely(ctrl_seg->wq_sig))
		*(start + 2) = htonl(~calc_xor(start, size << 4) << 24 | fm_ce_se);
}

static int __mlx5_post_send_one_other(struct ibv_exp_send_wr *wr,
		struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size)
{
	void *ctrl = seg;
	int err = 0;
	int size = 0;
	int num_sge = wr->num_sge;
	uint8_t fm_ce_se;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif

	if (unlikely(((MLX5_IB_OPCODE_GET_CLASS(mlx5_ib_opcode[wr->exp_opcode]) == MLX5_OPCODE_MANAGED) ||
		      (exp_send_flags & IBV_EXP_SEND_WITH_CALC)) &&
		     !(qp->gen_data.create_flags & IBV_EXP_QP_CREATE_CROSS_CHANNEL))) {
		mlx5_dbg(fp, MLX5_DBG_QP_SEND, "unsupported cross-channel functionality\n");
		return EINVAL;
	}

	seg += sizeof(struct mlx5_wqe_ctrl_seg);
	size = sizeof(struct mlx5_wqe_ctrl_seg) / 16;

	err = set_data_seg(qp, seg, &size,
			   !!(exp_send_flags & IBV_EXP_SEND_INLINE),
			   num_sge, wr->sg_list, 0, 0, 0, 0);
	if (unlikely(err))
		return err;

	fm_ce_se = qp->ctrl_seg.fm_ce_se_tbl[exp_send_flags &
					     (IBV_SEND_SOLICITED |
					      IBV_SEND_SIGNALED |
					      IBV_SEND_FENCE)];
	fm_ce_se |= get_fence(qp->gen_data.fm_cache, wr);
	set_ctrl_seg_sig(ctrl, &qp->ctrl_seg,
			 MLX5_IB_OPCODE_GET_OP(mlx5_ib_opcode[wr->exp_opcode]),
			 qp->gen_data.scur_post, 0, size, fm_ce_se,
			 send_ieth(wr));

	qp->gen_data.fm_cache = 0;
	*total_size = size;

	return 0;
}

#define ALIGN(x, log_a) ((((x) + (1 << (log_a)) - 1)) & ~((1 << (log_a)) - 1))

static inline uint16_t get_klm_octo(int nentries)
{
	return htons(ALIGN(nentries, 3) / 2);
}

static void set_umr_data_seg(struct mlx5_qp *qp, enum ibv_mw_type type,
			     int32_t rkey, struct ibv_mw_bind_info *bind_info,
			     uint32_t qpn, void **seg, int *size)
{
	union {
		struct mlx5_wqe_umr_klm_seg	klm;
		uint8_t				reserved[64];
	} *data = *seg;

	data->klm.byte_count = htonl(bind_info->length);
	data->klm.mkey = htonl(bind_info->mr->lkey);
	data->klm.address = htonll(bind_info->addr);

	memset(&data->klm + 1, 0, sizeof(data->reserved) -
	       sizeof(data->klm));

	*seg += sizeof(*data);
	*size += (sizeof(*data) / 16);
}

static void set_umr_mkey_seg(struct mlx5_qp *qp, enum ibv_mw_type type,
			     int32_t rkey, struct ibv_mw_bind_info *bind_info,
			     uint32_t qpn, void **seg, int *size)
{
	struct mlx5_wqe_mkey_context_seg	*mkey = *seg;

	mkey->qpn_mkey = htonl((rkey & 0xFF) |
				   ((type == IBV_MW_TYPE_1 || !bind_info->length) ?
				    0xFFFFFF00 : qpn << 8));
	if (bind_info->length) {
		/* Local read is set in kernel */
		mkey->access_flags = 0;
		mkey->free = 0;
		if (bind_info->mw_access_flags & IBV_ACCESS_LOCAL_WRITE)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_LOCAL_WRITE;
		if (bind_info->mw_access_flags & IBV_ACCESS_REMOTE_WRITE)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_WRITE;
		if (bind_info->mw_access_flags & IBV_ACCESS_REMOTE_READ)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_READ;
		if (bind_info->mw_access_flags & IBV_ACCESS_REMOTE_ATOMIC)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_ATOMIC;
		if (bind_info->mw_access_flags & IBV_ACCESS_ZERO_BASED)
			mkey->start_addr = 0;
		else
			mkey->start_addr = htonll(bind_info->addr);
		mkey->len = htonll(bind_info->length);
	} else {
		mkey->free = MLX5_WQE_MKEY_CONTEXT_FREE;
	}

	*seg += sizeof(struct mlx5_wqe_mkey_context_seg);
	*size += (sizeof(struct mlx5_wqe_mkey_context_seg) / 16);
}

static inline void set_umr_control_seg(struct mlx5_qp *qp, enum ibv_mw_type type,
				       int32_t rkey, struct ibv_mw_bind_info *bind_info,
				       uint32_t qpn, void **seg, int *size)
{
	struct mlx5_wqe_umr_ctrl_seg		*ctrl = *seg;

	ctrl->flags = MLX5_WQE_UMR_CTRL_FLAG_TRNSLATION_OFFSET |
		MLX5_WQE_UMR_CTRL_FLAG_INLINE;
	ctrl->mkey_mask = htonll(MLX5_WQE_UMR_CTRL_MKEY_MASK_FREE |
				     MLX5_WQE_UMR_CTRL_MKEY_MASK_MKEY);
	ctrl->translation_offset = 0;
	memset(ctrl->rsvd0, 0, sizeof(ctrl->rsvd0));
	memset(ctrl->rsvd1, 0, sizeof(ctrl->rsvd1));

	if (type == IBV_MW_TYPE_2)
		ctrl->mkey_mask |= htonll(MLX5_WQE_UMR_CTRL_MKEY_MASK_QPN);

	if (bind_info->length) {
		ctrl->klm_octowords = get_klm_octo(1);
		if (type == IBV_MW_TYPE_2)
			ctrl->flags |=  MLX5_WQE_UMR_CTRL_FLAG_CHECK_FREE;
		ctrl->mkey_mask |= htonll(MLX5_WQE_UMR_CTRL_MKEY_MASK_LEN	|
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_START_ADDR |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_LOCAL_WRITE |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_READ |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_WRITE |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_ATOMIC);
	} else {
		ctrl->klm_octowords = get_klm_octo(0);
		if (type == IBV_MW_TYPE_2)
			ctrl->flags |= MLX5_WQE_UMR_CTRL_FLAG_CHECK_QPN;
	}

	*seg += sizeof(struct mlx5_wqe_umr_ctrl_seg);
	*size += sizeof(struct mlx5_wqe_umr_ctrl_seg) / 16;
}

static inline int set_bind_wr(struct mlx5_qp *qp, enum ibv_mw_type type,
			      int32_t rkey, struct ibv_mw_bind_info *bind_info,
			      uint32_t qpn, void **seg, int *size)
{
	void *qend = qp->gen_data.sqend;

#ifdef MW_DEBUG
	if (bind_info->mw_access_flags &
	    ~(IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_READ |
	     IBV_ACCESS_REMOTE_WRITE))
		return EINVAL;

	if (bind_info->mr &&
	    (bind_info->mr->addr > (void *)bind_info->addr ||
	     bind_info->mr->addr + bind_info->mr->length <
	     (void *)bind_info->addr + bind_info->length ||
	     !(to_mmr(bind_info->mr)->alloc_flags &  IBV_ACCESS_MW_BIND) ||
	     (bind_info->mw_access_flags &
	      (IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_WRITE) &&
	      !(to_mmr(bind_info->mr)->alloc_flags & IBV_ACCESS_LOCAL_WRITE))))
		return EINVAL;

#endif

	/* check that len > 2GB because KLM support only 2GB */
	if (bind_info->length > 1UL << 31)
		return EOPNOTSUPP;

	set_umr_control_seg(qp, type, rkey, bind_info, qpn, seg, size);
	if (unlikely((*seg == qend)))
		*seg = mlx5_get_send_wqe(qp, 0);

	set_umr_mkey_seg(qp, type, rkey, bind_info, qpn, seg, size);
	if (!bind_info->length)
		return 0;

	if (unlikely((seg == qend)))
		*seg = mlx5_get_send_wqe(qp, 0);

	set_umr_data_seg(qp, type, rkey, bind_info, qpn, seg, size);
	return 0;
}

/* Copy tso header to eth segment with considering padding and WQE
 * wrap around in WQ buffer.
 */
static inline int set_tso_eth_seg(void **seg, struct ibv_exp_send_wr *wr,
				  struct mlx5_qp *qp, int *size)
{
	void *qend = qp->gen_data.sqend;
	struct mlx5_wqe_eth_seg *eseg = *seg;
	int size_of_inl_hdr_start = sizeof(eseg->inline_hdr_start);
	uint64_t left, left_len, copy_sz;
	void *pdata = wr->tso.hdr;
	int err = 0;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif

	if (unlikely(wr->tso.hdr_sz < MLX5_ETH_L2_MIN_HEADER_SIZE ||
		     wr->tso.hdr_sz > qp->max_tso_header)) {
		mlx5_dbg(fp, MLX5_DBG_QP_SEND,
			 "TSO header size should be at least %d and at most %d\n",
			 MLX5_ETH_L2_MIN_HEADER_SIZE,
			 qp->max_tso_header);
		return EINVAL;
	}

	left = wr->tso.hdr_sz;
	eseg->mss = htons(wr->tso.mss);
	eseg->inline_hdr_sz = htons(wr->tso.hdr_sz);

	/* Check if there is space till the end of queue, if yes,
	 * copy all in one shot, otherwise copy till the end of queue,
	 * rollback and then copy the left
	 */
	left_len = qend - (void *)eseg->inline_hdr_start;
	copy_sz = min(left_len, left);

	memcpy(eseg->inline_hdr_start, pdata, copy_sz);

	*seg += sizeof(struct mlx5_wqe_eth_seg);
	*size += sizeof(struct mlx5_wqe_eth_seg) / 16;

	/* The -1 is because there are already 16 bytes included in
	 * eseg->inline_hdr[16]
	 */
	*seg += align(copy_sz - size_of_inl_hdr_start, 16) - 16;
	*size += align(copy_sz - size_of_inl_hdr_start, 16) / 16 - 1;

	/* The last wqe in the queue */
	if (unlikely(copy_sz < left)) {
		*seg = mlx5_get_send_wqe(qp, 0);
		left -= copy_sz;
		pdata += copy_sz;
		memcpy(*seg, pdata, left);
		*seg += align(left, 16);
		*size += align(left, 16) / 16;
	}

	err = set_data_seg(qp, *seg, size, 0, wr->num_sge,
			   wr->sg_list, 0, 0, 0, 1);

	if (unlikely(err))
		return err;

	return 0;
}

static int __mlx5_post_send_one_raw_packet(struct ibv_exp_send_wr *wr,
					   struct mlx5_qp *qp,
					   uint64_t exp_send_flags, void *seg,
					   int *total_size) __MLX5_ALGN_F__;

static int __mlx5_post_send_one_raw_packet(struct ibv_exp_send_wr *wr,
					   struct mlx5_qp *qp,
					   uint64_t exp_send_flags, void *seg,
					   int *total_size)
{
	void *ctrl = seg;
	struct mlx5_wqe_eth_seg *eseg;
	int err = 0;
	int size = 0;
	int num_sge = wr->num_sge;
	int inl_hdr_size = to_mctx(qp->verbs_qp.qp.context)->eth_min_inline_size;
	int inl_hdr_copy_size = 0;
	int i = 0;
	uint8_t fm_ce_se;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif

	seg += sizeof(struct mlx5_wqe_ctrl_seg);
	size = sizeof(struct mlx5_wqe_ctrl_seg) / 16;

	eseg = seg;
	*((uint64_t *)eseg) = 0;
	eseg->rsvd2 = 0;

	if (exp_send_flags & IBV_EXP_SEND_IP_CSUM)
		eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM | MLX5_ETH_WQE_L4_CSUM;

	if (wr->exp_opcode == IBV_EXP_WR_TSO) {
		err = set_tso_eth_seg(&seg, wr, qp, &size);
	} else {
		/* The first bytes of the headers should be copied to the
		 * inline-headers of the ETH segment.
		 */
		if (likely(wr->sg_list[0].length >= MLX5_ETH_INLINE_HEADER_SIZE)) {
			inl_hdr_copy_size = inl_hdr_size;
			memcpy(eseg->inline_hdr_start,
			       (void *)(uintptr_t)wr->sg_list[0].addr,
			       inl_hdr_copy_size);
		} else {
			for (i = 0; i < num_sge && inl_hdr_size > 0; ++i) {
				inl_hdr_copy_size = min(wr->sg_list[i].length,
							inl_hdr_size);
				memcpy(eseg->inline_hdr_start +
				       (MLX5_ETH_INLINE_HEADER_SIZE - inl_hdr_size),
				       (void *)(uintptr_t)wr->sg_list[i].addr,
				       inl_hdr_copy_size);
				inl_hdr_size -= inl_hdr_copy_size;
			}
			--i;
			if (unlikely(inl_hdr_size)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "Ethernet headers < %d bytes\n",
					 MLX5_ETH_INLINE_HEADER_SIZE);
				return EINVAL;
			}
		}

		eseg->inline_hdr_sz = htons(inl_hdr_size);

		seg += (offsetof(struct mlx5_wqe_eth_seg, inline_hdr) + inl_hdr_size) & ~0xf;
		size += (offsetof(struct mlx5_wqe_eth_seg, inline_hdr) + inl_hdr_size) >> 4;

		/* If we copied all the sge into the inline-headers, then we need to
		 * start copying from the next sge into the data-segment.
		 */
		if (unlikely(wr->sg_list[i].length == inl_hdr_copy_size)) {
			++i;
			inl_hdr_copy_size = 0;
		}

		/* The copied headers should be excluded from the data segment */
		err = set_data_seg(qp, seg, &size,
				   !!(exp_send_flags & IBV_EXP_SEND_INLINE),
				   num_sge, wr->sg_list, 0, i, inl_hdr_copy_size, 0);
	}

	if (unlikely(err))
		return err;

	fm_ce_se = qp->ctrl_seg.fm_ce_se_tbl[exp_send_flags &
					     (IBV_SEND_SOLICITED |
					      IBV_SEND_SIGNALED |
					      IBV_SEND_FENCE)];
	fm_ce_se |= get_fence(qp->gen_data.fm_cache, wr);
	set_ctrl_seg_sig(ctrl, &qp->ctrl_seg,
			 MLX5_IB_OPCODE_GET_OP(mlx5_ib_opcode[wr->exp_opcode]),
			 qp->gen_data.scur_post, 0, size, fm_ce_se,
			 send_ieth(wr));

	qp->gen_data.fm_cache = 0;
	*total_size = size;

	return 0;
}

static int ____mlx5_post_send_one_underlay(struct ibv_exp_send_wr *wr,
					   struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size) __MLX5_ALGN_F__;
static int ____mlx5_post_send_one_underlay(struct ibv_exp_send_wr *wr,
					   struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size)
{
	void *ctrl = seg;
	int err = 0;
	int size = 0;
	int num_sge = wr->num_sge;
	uint8_t fm_ce_se;
	int tmp;
	struct mlx5_wqe_eth_pad *pad;
	struct mlx5_wqe_eth_seg *eseg;
	int inl_hdr_copy_size;
	int sg_start_entry = 0;
	int is_inl = !!(exp_send_flags & IBV_EXP_SEND_INLINE);

	if (unlikely(wr->exp_opcode == IBV_EXP_WR_SEND_WITH_IMM))
		return EINVAL;

	seg += sizeof(struct mlx5_wqe_ctrl_seg);
	size = sizeof(struct mlx5_wqe_ctrl_seg) / 16;

	tmp = set_ipoib_datagram_seg(seg, wr);
	seg  += tmp;
	size += (tmp >> 4);
	if (unlikely((seg == qp->gen_data.sqend)))
		seg = mlx5_get_send_wqe(qp, 0);

	pad = seg;
	memset(pad, 0, sizeof(struct mlx5_wqe_eth_pad));
	seg += sizeof(struct mlx5_wqe_eth_pad);
	size += sizeof(struct mlx5_wqe_eth_pad) / 16;
	eseg = seg;
	*((uint64_t *)eseg) = 0;
	eseg->rsvd2 = 0;

	if (exp_send_flags & IBV_EXP_SEND_IP_CSUM)
		eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM | MLX5_ETH_WQE_L4_CSUM;

	if (likely(wr->sg_list[0].length >= MLX5_IPOIB_INLINE_MAX_HEADER_SIZE))
		/* Copying the minimum required data unless inline mode is set */
		inl_hdr_copy_size = is_inl ? MLX5_IPOIB_INLINE_MAX_HEADER_SIZE : MLX5_IPOIB_INLINE_MIN_HEADER_SIZE;
	else {
		inl_hdr_copy_size = MLX5_IPOIB_INLINE_MIN_HEADER_SIZE;
		/* We expect at least 4 bytes as part of first entry to hold the IPoIB header */
		if (unlikely(wr->sg_list[0].length < inl_hdr_copy_size))
			return EINVAL;
	}

	memcpy(eseg->inline_hdr_start, (void *)(uintptr_t)wr->sg_list[0].addr,
	       inl_hdr_copy_size);
	eseg->inline_hdr_sz = htons(inl_hdr_copy_size);
	size += sizeof(struct mlx5_wqe_eth_seg) / 16;
	seg += sizeof(struct mlx5_wqe_eth_seg);

	/* If we copied all the sge into the inline-headers, then we need to
	 * start copying from the next sge into the data-segment.
	 */
	if (unlikely(wr->sg_list[0].length == inl_hdr_copy_size)) {
		sg_start_entry++;
		inl_hdr_copy_size = 0;
	}

	err = set_data_seg(qp, seg, &size, is_inl, num_sge, wr->sg_list, 0, sg_start_entry, inl_hdr_copy_size, 0);
	if (unlikely(err))
		return err;

	fm_ce_se = qp->ctrl_seg.fm_ce_se_tbl[exp_send_flags & (IBV_SEND_SOLICITED | IBV_SEND_SIGNALED | IBV_SEND_FENCE)];
	fm_ce_se |= get_fence(qp->gen_data.fm_cache, wr);
	set_ctrl_seg_sig(ctrl, &qp->ctrl_seg, MLX5_IB_OPCODE_GET_OP(mlx5_ib_opcode[wr->exp_opcode]),
			 qp->gen_data.scur_post, 0, size, fm_ce_se, send_ieth(wr));

	qp->gen_data.fm_cache = 0;
	*total_size = size;

	return 0;
}

static int __mlx5_post_send_one_uc_ud(struct ibv_exp_send_wr *wr,
		struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size) __MLX5_ALGN_F__;
static int __mlx5_post_send_one_uc_ud(struct ibv_exp_send_wr *wr,
		struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size)
{
	void *ctrl = seg;
	int err = 0;
	int size = 0;
	int num_sge = wr->num_sge;
	uint8_t fm_ce_se;
	int tmp;
	uint8_t next_fence = 0;
	uint32_t imm;

#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif


	if (unlikely(((MLX5_IB_OPCODE_GET_CLASS(mlx5_ib_opcode[wr->exp_opcode]) == MLX5_OPCODE_MANAGED) ||
		      (exp_send_flags & IBV_EXP_SEND_WITH_CALC)) &&
		     !(qp->gen_data.create_flags & IBV_EXP_QP_CREATE_CROSS_CHANNEL))) {
		mlx5_dbg(fp, MLX5_DBG_QP_SEND, "unsupported cross-channel functionality\n");
		return EINVAL;
	}

	seg += sizeof(struct mlx5_wqe_ctrl_seg);
	size = sizeof(struct mlx5_wqe_ctrl_seg) / 16;
	imm = send_ieth(wr);

	switch (qp->gen_data_warm.qp_type) {
	case IBV_QPT_UC:
		switch (wr->exp_opcode) {
		case IBV_WR_RDMA_WRITE:
		case IBV_WR_RDMA_WRITE_WITH_IMM:
			set_raddr_seg(seg, wr->wr.rdma.remote_addr,
				      wr->wr.rdma.rkey);
			seg  += sizeof(struct mlx5_wqe_raddr_seg);
			size += sizeof(struct mlx5_wqe_raddr_seg) / 16;
			break;

		case IBV_EXP_WR_BIND_MW:
		{
			/* Must use the upstream layout of struct ibv_send_wr */
			struct ibv_send_wr *send_wr = ( struct ibv_send_wr *)wr;

			next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
			imm = htonl(send_wr->bind_mw.mw->rkey);
			err = set_bind_wr(qp, send_wr->bind_mw.mw->type,
					  send_wr->bind_mw.rkey,
					  &send_wr->bind_mw.bind_info,
					  qp->verbs_qp.qp.qp_num, &seg, &size);
			if (err)
				return err;

			qp->sq.wr_data[qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1)] = IBV_WC_BIND_MW;
			break;
		}

		case IBV_EXP_WR_LOCAL_INV:
		{
			struct ibv_mw_bind_info	bind_info = {};
			struct ibv_send_wr *send_wr = ( struct ibv_send_wr *)wr;

			next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
			imm = htonl(send_wr->imm_data);
			err = set_bind_wr(qp, IBV_MW_TYPE_2, 0,
					  &bind_info, qp->verbs_qp.qp.qp_num,
					  &seg, &size);
			if (err)
				return err;

			qp->sq.wr_data[qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1)] = IBV_WC_LOCAL_INV;
			break;
		}
		default:
			break;
		}
		break;

	case IBV_QPT_UD:
		tmp = set_datagram_seg(seg, wr);
		seg  += tmp;
		size += (tmp >> 4);
		if (unlikely((seg == qp->gen_data.sqend)))
			seg = mlx5_get_send_wqe(qp, 0);
		break;

	default:
		break;
	}

	err = set_data_seg(qp, seg, &size, !!(exp_send_flags & IBV_EXP_SEND_INLINE),
			   num_sge, wr->sg_list, 0, 0, 0, 0);
	if (unlikely(err))
		return err;

	fm_ce_se = qp->ctrl_seg.fm_ce_se_tbl[exp_send_flags & (IBV_SEND_SOLICITED | IBV_SEND_SIGNALED | IBV_SEND_FENCE)];
	fm_ce_se |= get_fence(qp->gen_data.fm_cache, wr);
	set_ctrl_seg_sig(ctrl, &qp->ctrl_seg, MLX5_IB_OPCODE_GET_OP(mlx5_ib_opcode[wr->exp_opcode]),
			 qp->gen_data.scur_post, 0, size, fm_ce_se, imm);

	qp->gen_data.fm_cache = next_fence;
	*total_size = size;

	return 0;
}
static int __mlx5_post_send_one_rc_dc(struct ibv_exp_send_wr *wr,
				      struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size) __MLX5_ALGN_F__;
static int __mlx5_post_send_one_rc_dc(struct ibv_exp_send_wr *wr,
				      struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size)
{
	struct mlx5_klm_buf *klm;
	void *ctrl = seg;
	struct ibv_qp *ibqp = &qp->verbs_qp.qp;
	struct mlx5_context *ctx = to_mctx(ibqp->context);
	int err = 0;
	int size = 0;
	uint8_t opmod = 0;
	void *qend = qp->gen_data.sqend;
	uint32_t mlx5_opcode;
	struct mlx5_wqe_xrc_seg *xrc;
	int tmp = 0;
	int num_sge = wr->num_sge;
	uint8_t next_fence = 0;
	struct mlx5_wqe_umr_ctrl_seg *umr_ctrl;
	int xlat_size;
	struct mlx5_mkey_seg *mk;
	int wqe_sz;
	uint64_t reglen;
	int atom_arg = 0;
	uint8_t fm_ce_se;
	uint32_t imm;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif


	if (unlikely(((MLX5_IB_OPCODE_GET_CLASS(mlx5_ib_opcode[wr->exp_opcode]) == MLX5_OPCODE_MANAGED) ||
		      (exp_send_flags & IBV_EXP_SEND_WITH_CALC)) &&
		     !(qp->gen_data.create_flags & IBV_EXP_QP_CREATE_CROSS_CHANNEL))) {
		mlx5_dbg(fp, MLX5_DBG_QP_SEND, "unsupported cross-channel functionality\n");
		return EINVAL;
	}

	mlx5_opcode = MLX5_IB_OPCODE_GET_OP(mlx5_ib_opcode[wr->exp_opcode]);
	imm = send_ieth(wr);

	seg += sizeof(struct mlx5_wqe_ctrl_seg);
	size = sizeof(struct mlx5_wqe_ctrl_seg) / 16;

	switch (qp->gen_data_warm.qp_type) {
	case IBV_QPT_XRC_SEND:
	case IBV_QPT_XRC:
	case IBV_EXP_QPT_DC_INI:
		if (qp->gen_data_warm.qp_type == IBV_EXP_QPT_DC_INI) {
			if (likely(wr->exp_opcode != IBV_EXP_WR_NOP))
				tmp = set_dci_seg(seg, wr);
			seg  += tmp;
			size += (tmp >> 4);
			if (unlikely((seg == qend)))
				seg = mlx5_get_send_wqe(qp, 0);

		} else {
			if (wr->exp_opcode != IBV_EXP_WR_BIND_MW &&
				wr->exp_opcode != IBV_EXP_WR_LOCAL_INV) {
				xrc = seg;
				xrc->xrc_srqn = htonl(wr->qp_type.xrc.remote_srqn);
				seg += sizeof(*xrc);
				size += sizeof(*xrc) / 16;
			}
		}
		/* fall through */
	case IBV_QPT_RC:
		switch (wr->exp_opcode) {
		case IBV_EXP_WR_RDMA_READ:
		case IBV_EXP_WR_RDMA_WRITE:
		case IBV_EXP_WR_RDMA_WRITE_WITH_IMM:
			if (unlikely(exp_send_flags & IBV_EXP_SEND_WITH_CALC)) {

				if ((uint32_t)wr->op.calc.data_size >= IBV_EXP_CALC_DATA_SIZE_NUMBER ||
				    (uint32_t)wr->op.calc.calc_op >= IBV_EXP_CALC_OP_NUMBER ||
				    (uint32_t)wr->op.calc.data_type >= IBV_EXP_CALC_DATA_TYPE_NUMBER ||
				    !mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
							[wr->op.calc.data_type].valid)
					return EINVAL;

				opmod = mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
								  [wr->op.calc.data_type].opmod;
			}
			set_raddr_seg(seg, wr->wr.rdma.remote_addr, wr->wr.rdma.rkey);
			seg  += sizeof(struct mlx5_wqe_raddr_seg);
			size += sizeof(struct mlx5_wqe_raddr_seg) / 16;
			break;

		case IBV_EXP_WR_ATOMIC_CMP_AND_SWP:
		case IBV_EXP_WR_ATOMIC_FETCH_AND_ADD:
			if (unlikely(!qp->enable_atomics)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "atomics not allowed\n");
				return EINVAL;
			}
			set_raddr_seg(seg, wr->wr.atomic.remote_addr,
				      wr->wr.atomic.rkey);
			seg  += sizeof(struct mlx5_wqe_raddr_seg);

			set_atomic_seg(seg, wr->exp_opcode, wr->wr.atomic.swap,
				       wr->wr.atomic.compare_add);
			seg  += sizeof(struct mlx5_wqe_atomic_seg);

			size += (sizeof(struct mlx5_wqe_raddr_seg) +
			sizeof(struct mlx5_wqe_atomic_seg)) / 16;
			atom_arg = 8;
			break;

		case IBV_EXP_WR_EXT_MASKED_ATOMIC_CMP_AND_SWP:
		case IBV_EXP_WR_EXT_MASKED_ATOMIC_FETCH_AND_ADD:
			if (unlikely(!qp->enable_atomics)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "atomics not allowed\n");
				return EINVAL;
			}
			if (unlikely(wr->ext_op.masked_atomics.log_arg_sz >=
						 sizeof(ctx->info.bit_mask_log_atomic_arg_sizes) * 8)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "too big atomic arg\n");
				return EINVAL;
			}
			atom_arg = 1 << wr->ext_op.masked_atomics.log_arg_sz;
			if (unlikely(!(ctx->info.bit_mask_log_atomic_arg_sizes & atom_arg))) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "unsupported atomic arg size. supported bitmask 0x%lx\n",
					 (unsigned long)ctx->info.bit_mask_log_atomic_arg_sizes);
				return EINVAL;
			}

			set_raddr_seg(seg, wr->ext_op.masked_atomics.remote_addr,
				      wr->ext_op.masked_atomics.rkey);
			seg  += sizeof(struct mlx5_wqe_raddr_seg);
			size += sizeof(struct mlx5_wqe_raddr_seg) / 16;
			tmp = set_ext_atomic_seg(qp, seg, wr);
			if (unlikely(tmp < 0)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "invalid atomic arguments\n");
				return EINVAL;
			}
			size += (tmp >> 4);
			seg += tmp;
			if (unlikely((seg >= qend)))
				seg = seg - qend + mlx5_get_send_wqe(qp, 0);
			opmod = MLX5_OPCODE_EXT_ATOMICS | (wr->ext_op.masked_atomics.log_arg_sz - 2);
			break;

		case IBV_EXP_WR_SEND:
			if (unlikely(exp_send_flags & IBV_EXP_SEND_WITH_CALC)) {

				if ((uint32_t)wr->op.calc.data_size >= IBV_EXP_CALC_DATA_SIZE_NUMBER ||
				    (uint32_t)wr->op.calc.calc_op >= IBV_EXP_CALC_OP_NUMBER ||
				    (uint32_t)wr->op.calc.data_type >= IBV_EXP_CALC_DATA_TYPE_NUMBER ||
				    !mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
							[wr->op.calc.data_type].valid)
					return EINVAL;

				opmod = mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
								  [wr->op.calc.data_type].opmod;
			}
			break;

		case IBV_EXP_WR_CQE_WAIT:
			{
				struct mlx5_cq *wait_cq = to_mcq(wr->task.cqe_wait.cq);
				uint32_t wait_index = 0;

				wait_index = wait_cq->wait_index +
						wr->task.cqe_wait.cq_count;
				wait_cq->wait_count = max(wait_cq->wait_count,
						wr->task.cqe_wait.cq_count);

				if (exp_send_flags & IBV_EXP_SEND_WAIT_EN_LAST) {
					wait_cq->wait_index += wait_cq->wait_count;
					wait_cq->wait_count = 0;
				}

				set_wait_en_seg(seg, wait_cq->cqn, wait_index);
				seg   += sizeof(struct mlx5_wqe_wait_en_seg);
				size += sizeof(struct mlx5_wqe_wait_en_seg) / 16;
			}
			break;

		case IBV_EXP_WR_SEND_ENABLE:
		case IBV_EXP_WR_RECV_ENABLE:
			{
				unsigned head_en_index;
				struct mlx5_wq *wq;
				struct mlx5_wq_recv_send_enable *wq_en;

				/*
				 * Posting work request for QP that does not support
				 * SEND/RECV ENABLE makes performance worse.
				 */
				if (((wr->exp_opcode == IBV_EXP_WR_SEND_ENABLE) &&
					!(to_mqp(wr->task.wqe_enable.qp)->gen_data.create_flags &
						IBV_EXP_QP_CREATE_MANAGED_SEND)) ||
					((wr->exp_opcode == IBV_EXP_WR_RECV_ENABLE) &&
					!(to_mqp(wr->task.wqe_enable.qp)->gen_data.create_flags &
						IBV_EXP_QP_CREATE_MANAGED_RECV))) {
					return EINVAL;
				}

				wq = (wr->exp_opcode == IBV_EXP_WR_SEND_ENABLE) ?
					&to_mqp(wr->task.wqe_enable.qp)->sq :
					&to_mqp(wr->task.wqe_enable.qp)->rq;

				wq_en = (wr->exp_opcode == IBV_EXP_WR_SEND_ENABLE) ?
					 &to_mqp(wr->task.wqe_enable.qp)->sq_enable :
					 &to_mqp(wr->task.wqe_enable.qp)->rq_enable;

				/* If wqe_count is 0 release all WRs from queue */
				if (wr->task.wqe_enable.wqe_count) {
					head_en_index = wq_en->head_en_index +
								wr->task.wqe_enable.wqe_count;
					wq_en->head_en_count = max(wq_en->head_en_count,
								   wr->task.wqe_enable.wqe_count);

					if ((int)(wq->head - head_en_index) < 0)
						return EINVAL;
				} else {
					head_en_index = wq->head;
					wq_en->head_en_count = wq->head - wq_en->head_en_index;
				}

				if (exp_send_flags & IBV_EXP_SEND_WAIT_EN_LAST) {
					wq_en->head_en_index += wq_en->head_en_count;
					wq_en->head_en_count = 0;
				}

				set_wait_en_seg(seg,
						wr->task.wqe_enable.qp->qp_num,
						head_en_index);

				seg += sizeof(struct mlx5_wqe_wait_en_seg);
				size += sizeof(struct mlx5_wqe_wait_en_seg) / 16;
			}
			break;
		case IBV_EXP_WR_UMR_FILL:
		case IBV_EXP_WR_UMR_INVALIDATE:
			if (unlikely(!qp->umr_en)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "UMR not supported\n");
				return EINVAL;
			}
			next_fence = MLX5_FENCE_MODE_INITIATOR_SMALL;
			imm = htonl(wr->ext_op.umr.modified_mr->lkey);
			num_sge = 0;
			umr_ctrl = seg;
			set_umr_ctrl_seg(wr, seg);
			seg += sizeof(struct mlx5_wqe_umr_ctrl_seg);
			size += sizeof(struct mlx5_wqe_umr_ctrl_seg) / 16;

			if (unlikely((seg == qend)))
				seg = mlx5_get_send_wqe(qp, 0);
			mk = seg;
			set_mkey_seg(wr, seg);
			seg += sizeof(*mk);
			size += (sizeof(*mk) / 16);
			qp->sq.wr_data[qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1)] = IBV_EXP_WC_UMR;
			if (wr->exp_opcode == IBV_EXP_WR_UMR_INVALIDATE)
				break;

			if (unlikely((seg == qend)))
				seg = mlx5_get_send_wqe(qp, 0);
			err = lay_umr(qp, wr, seg, &wqe_sz, &xlat_size, &reglen);
			if (err) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "lay_umr failure\n");
				return err;
			}
			mk->len = htonll(reglen);
			size += wqe_sz / 16;
			seg += wqe_sz;
			umr_ctrl->klm_octowords = htons(align(xlat_size, 64) / 16);
			if (unlikely((seg >= qend)))
				seg = adjust_seg(qp, seg);
			if (!(wr->exp_send_flags & IBV_EXP_SEND_INLINE)) {
				struct ibv_sge sge;

				klm = to_klm(wr->ext_op.umr.memory_objects);
				sge.addr = (uint64_t)(uintptr_t)klm->mr->addr;
				sge.lkey = klm->mr->lkey;
				sge.length = 0;
				set_data_ptr_seg(seg, &sge, qp, 0);
				size += sizeof(struct mlx5_wqe_data_seg) / 16;
				seg += sizeof(struct mlx5_wqe_data_seg);
			}
			break;

		case IBV_EXP_WR_BIND_MW:
		{
			/* Must use the upstream layout of struct ibv_send_wr */
			struct ibv_send_wr *send_wr = ( struct ibv_send_wr *)wr;
			next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
			imm = htonl(send_wr->bind_mw.mw->rkey);
			err = set_bind_wr(qp, send_wr->bind_mw.mw->type,
					  send_wr->bind_mw.rkey,
					  &send_wr->bind_mw.bind_info,
					  ibqp->qp_num, &seg, &size);
			if (err)
				return err;

			qp->sq.wr_data[qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1)] = IBV_WC_BIND_MW;
			break;
		}

		case IBV_EXP_WR_LOCAL_INV:
		{
			struct ibv_mw_bind_info	bind_info = {};
			struct ibv_send_wr *send_wr = ( struct ibv_send_wr *)wr;

			next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
			imm = htonl(send_wr->imm_data);
			err = set_bind_wr(qp, IBV_MW_TYPE_2, 0,
					  &bind_info, ibqp->qp_num,
					  &seg, &size);
			if (err)
				return err;

			qp->sq.wr_data[qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1)] = IBV_WC_LOCAL_INV;
			break;
		}

		case IBV_EXP_WR_NOP:
			break;
		case IBV_EXP_WR_SEND_WITH_INV:
			imm = htonl(wr->ex.invalidate_rkey);
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}

	err = set_data_seg(qp, seg, &size, !!(exp_send_flags & IBV_EXP_SEND_INLINE),
			   num_sge, wr->sg_list, atom_arg, 0, 0, 0);
	if (unlikely(err))
		return err;

	fm_ce_se = qp->ctrl_seg.fm_ce_se_tbl[exp_send_flags & (IBV_SEND_SOLICITED | IBV_SEND_SIGNALED | IBV_SEND_FENCE)];
	fm_ce_se |= get_fence(qp->gen_data.fm_cache, wr);
	set_ctrl_seg_sig(ctrl, &qp->ctrl_seg,
			 mlx5_opcode, qp->gen_data.scur_post, opmod, size,
			 fm_ce_se, imm);

	qp->gen_data.fm_cache = next_fence;
	*total_size = size;

	return 0;
}

static inline int __mlx5_post_send_one_fast_rc(struct ibv_exp_send_wr *wr,
			struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size,
			const int cmd, const int inl) __attribute__((always_inline));
static inline int __mlx5_post_send_one_fast_rc(struct ibv_exp_send_wr *wr,
			struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size,
			const int cmd, const int inl)
{
	struct mlx5_wqe_ctrl_seg *ctrl = seg;
	int err = 0;
	int size = 0;
	uint8_t fm_ce_se;

	seg += sizeof(*ctrl);
	size = sizeof(*ctrl) / 16;

	if (cmd == MLX5_OPCODE_RDMA_WRITE) {
		set_raddr_seg(seg, wr->wr.rdma.remote_addr, wr->wr.rdma.rkey);
		seg  += sizeof(struct mlx5_wqe_raddr_seg);
		size += sizeof(struct mlx5_wqe_raddr_seg) / 16;
	}

	if (inl)
		err = set_data_inl_seg(qp, wr->num_sge, wr->sg_list, seg,
				       &size, 0, 0);
	else
		err = set_data_non_inl_seg(qp, wr->num_sge, wr->sg_list, seg,
					   &size, 0, 0, 0);
	if (unlikely(err))
		return err;

	fm_ce_se = qp->ctrl_seg.fm_ce_se_tbl[exp_send_flags & (IBV_SEND_SOLICITED | IBV_SEND_SIGNALED | IBV_SEND_FENCE)];
	if (unlikely(qp->gen_data.fm_cache)) {
		if (unlikely(exp_send_flags & IBV_EXP_SEND_FENCE))
			fm_ce_se |= MLX5_FENCE_MODE_SMALL_AND_FENCE;
		else
			fm_ce_se |= qp->gen_data.fm_cache;
	}

	set_ctrl_seg((uint32_t *)ctrl, &qp->ctrl_seg,
		     cmd, qp->gen_data.scur_post, 0, size,
		     fm_ce_se, 0);

	qp->gen_data.fm_cache = 0;
	*total_size = size;

	return 0;
}

#define MLX5_POST_SEND_ONE_FAST_RC(suffix, cmd, inl)			\
	static int __mlx5_post_send_one_fast_rc_##suffix(		\
			struct ibv_exp_send_wr *wr,			\
			struct mlx5_qp *qp, uint64_t exp_send_flags,	\
			void *seg, int *total_size) __MLX5_ALGN_F__;	\
	static int __mlx5_post_send_one_fast_rc_##suffix(		\
			struct ibv_exp_send_wr *wr,			\
			struct mlx5_qp *qp, uint64_t exp_send_flags,	\
			void *seg, int *total_size)			\
	{								\
		return __mlx5_post_send_one_fast_rc(wr, qp,		\
						    exp_send_flags,	\
						    seg, total_size,	\
						    cmd, inl);		\
	}
/*			   suffix		cmd			inl */
MLX5_POST_SEND_ONE_FAST_RC(send,		MLX5_OPCODE_SEND,	0);
MLX5_POST_SEND_ONE_FAST_RC(send_inl,		MLX5_OPCODE_SEND,	1);
MLX5_POST_SEND_ONE_FAST_RC(rwrite,		MLX5_OPCODE_RDMA_WRITE,	0);
MLX5_POST_SEND_ONE_FAST_RC(rwrite_inl,		MLX5_OPCODE_RDMA_WRITE,	1);

static int __mlx5_post_send_one_not_ready(struct ibv_exp_send_wr *wr,
			struct mlx5_qp *qp, uint64_t exp_send_flags, void *seg, int *total_size)
{
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif
	mlx5_dbg(fp, MLX5_DBG_QP_SEND, "bad QP state\n");

	return EINVAL;
}

enum  mlx5_post_send_one_rc_cases {
	MLX5_SEND_RC		= (IBV_EXP_WR_SEND),
	MLX5_SEND_RC_INL	= (IBV_EXP_WR_SEND) + (IBV_EXP_SEND_INLINE << 8),
	MLX5_RDMA_WRITE_RC	= (IBV_EXP_WR_RDMA_WRITE),
	MLX5_RDMA_WRITE_RC_INL	= (IBV_EXP_WR_RDMA_WRITE) + (IBV_EXP_SEND_INLINE << 8),
};

static int __mlx5_post_send_one_rc(struct ibv_exp_send_wr *wr,
				   struct mlx5_qp *qp, uint64_t exp_send_flags,
				   void *seg, int *total_size) __MLX5_ALGN_F__;
static int __mlx5_post_send_one_rc(struct ibv_exp_send_wr *wr,
				   struct mlx5_qp *qp, uint64_t exp_send_flags,
				   void *seg, int *total_size)
{
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(qp->verbs_qp.qp.context)->dbg_fp;
#endif
	uint64_t rc_case = (uint64_t)wr->exp_opcode | ((exp_send_flags & (IBV_EXP_SEND_WITH_CALC | IBV_EXP_SEND_INLINE)) <<  8);

	switch (rc_case) {

	case MLX5_SEND_RC:
		return __mlx5_post_send_one_fast_rc_send(wr, qp, exp_send_flags, seg, total_size);

	case MLX5_SEND_RC_INL:
		return __mlx5_post_send_one_fast_rc_send_inl(wr, qp, exp_send_flags, seg, total_size);

	case MLX5_RDMA_WRITE_RC:
		return __mlx5_post_send_one_fast_rc_rwrite(wr, qp, exp_send_flags, seg, total_size);

	case MLX5_RDMA_WRITE_RC_INL:
		return __mlx5_post_send_one_fast_rc_rwrite_inl(wr, qp, exp_send_flags, seg, total_size);

	default:
		if (unlikely(wr->exp_opcode < 0 ||
		    wr->exp_opcode >= sizeof(mlx5_ib_opcode) / sizeof(mlx5_ib_opcode[0]))) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "bad opcode %d\n", wr->exp_opcode);
			return EINVAL;
		} else {
			return __mlx5_post_send_one_rc_dc(wr, qp, exp_send_flags, seg, total_size);
		}
	}
}

void mlx5_update_post_send_one(struct mlx5_qp *qp, enum ibv_qp_state qp_state, enum ibv_qp_type	qp_type)
{
	if (qp_state <  IBV_QPS_RTS) {
		qp->gen_data.post_send_one = __mlx5_post_send_one_not_ready;
	} else {
		switch (qp_type) {
		case IBV_QPT_XRC_SEND:
		case IBV_QPT_XRC:
		case IBV_EXP_QPT_DC_INI:
			qp->gen_data.post_send_one = __mlx5_post_send_one_rc_dc;
			break;
		case IBV_QPT_RC:
			if (qp->ctrl_seg.wq_sig)
				qp->gen_data.post_send_one = __mlx5_post_send_one_rc_dc;
			else
				qp->gen_data.post_send_one = __mlx5_post_send_one_rc;

			break;

		case IBV_QPT_UC:
		case IBV_QPT_UD:
			if (qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY)
				qp->gen_data.post_send_one = ____mlx5_post_send_one_underlay;
			else
				qp->gen_data.post_send_one = __mlx5_post_send_one_uc_ud;
			break;

		case IBV_QPT_RAW_ETH:
			qp->gen_data.post_send_one = __mlx5_post_send_one_raw_packet;
			break;

		default:
			qp->gen_data.post_send_one = __mlx5_post_send_one_other;
			break;
		}
	}
}

static inline int __mlx5_post_send(struct ibv_qp *ibqp, struct ibv_exp_send_wr *wr,
				   struct ibv_exp_send_wr **bad_wr, int is_exp_wr) __attribute__((always_inline));
static inline int __mlx5_post_send(struct ibv_qp *ibqp, struct ibv_exp_send_wr *wr,
				   struct ibv_exp_send_wr **bad_wr, int is_exp_wr)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	void *uninitialized_var(seg);
	void *uninitialized_var(wqe2ring);
	int nreq;
	int err = 0;
	int size;
	unsigned idx;
	uint64_t exp_send_flags;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(ibqp->context)->dbg_fp;
#endif
	mlx5_lock(&qp->sq.lock);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		idx = qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1);
		seg = mlx5_get_send_wqe(qp, idx);

		exp_send_flags = is_exp_wr ? wr->exp_send_flags : ((struct ibv_send_wr *)wr)->send_flags;

		if (unlikely(!(qp->gen_data.create_flags & IBV_EXP_QP_CREATE_IGNORE_SQ_OVERFLOW) &&
			     mlx5_wq_overflow(0, nreq, qp))) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "work queue overflow\n");
			errno = ENOMEM;
			err = errno;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->sq.max_gs)) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "max gs exceeded %d (max = %d)\n",
				 wr->num_sge, qp->sq.max_gs);
			errno = ENOMEM;
			err = errno;
			*bad_wr = wr;
			goto out;
		}



		err = qp->gen_data.post_send_one(wr, qp, exp_send_flags, seg, &size);
		if (unlikely(err)) {
			errno = err;
			*bad_wr = wr;
			goto out;
		}



		qp->sq.wrid[idx] = wr->wr_id;
		qp->gen_data.wqe_head[idx] = qp->sq.head + nreq;
		qp->gen_data.scur_post += DIV_ROUND_UP(size * 16, MLX5_SEND_WQE_BB);

		wqe2ring = seg;

#ifdef MLX5_DEBUG
		if (mlx5_debug_mask & MLX5_DBG_QP_SEND)
			dump_wqe(to_mctx(ibqp->context)->dbg_fp, idx, size, qp);
#endif
	}

out:
	if (likely(nreq)) {
		qp->sq.head += nreq;

		if (unlikely(qp->gen_data.create_flags
					& CREATE_FLAG_NO_DOORBELL)) {
			/* Controlled or peer-direct qp */
			wmb();
			if (qp->peer_enabled)
				qp->peer_ctrl_seg = wqe2ring;
			goto post_send_no_db;
		}

		__ring_db(qp, qp->gen_data.bf->db_method, qp->gen_data.scur_post & 0xffff, wqe2ring, (size + 3) / 4);
	}

post_send_no_db:

	mlx5_unlock(&qp->sq.lock);

	return err;
}

int mlx5_exp_peer_commit_qp(struct ibv_qp *ibqp,
			struct ibv_exp_peer_commit *commit_ctx)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	struct peer_op_wr *wr = commit_ctx->storage;
	int entries = 3;

	if (!qp->peer_enabled)
		return -EINVAL;

	if (commit_ctx->entries < entries)
		return -ENOSPC;

	if (!qp->peer_ctrl_seg) {
		/* nothing to commit */
		commit_ctx->entries = 0;
		return 0;
	}

	commit_ctx->rollback_id = qp->peer_scur_post |
		((uint64_t)qp->gen_data.scur_post << 32);
	qp->peer_scur_post = qp->gen_data.scur_post;

	wr->type = IBV_EXP_PEER_OP_STORE_DWORD;
	wr->wr.dword_va.data = htonl(qp->gen_data.scur_post & 0xffff);
	wr->wr.dword_va.target_id = qp->peer_va_ids[MLX5_QP_PEER_VA_ID_DBR];
	wr->wr.dword_va.offset = sizeof(uint32_t) * MLX5_SND_DBR;
	wr = wr->next;

	wr->type = IBV_EXP_PEER_OP_FENCE;
	wr->wr.fence.fence_flags = IBV_EXP_PEER_FENCE_OP_WRITE |
				   IBV_EXP_PEER_FENCE_FROM_HCA;
	if (qp->peer_db_buf)
		wr->wr.fence.fence_flags |= IBV_EXP_PEER_FENCE_MEM_PEER;
	else
		wr->wr.fence.fence_flags |= IBV_EXP_PEER_FENCE_MEM_SYS;
	wr = wr->next;

	wr->type = IBV_EXP_PEER_OP_STORE_QWORD;
	wr->wr.qword_va.data = *(__be64 *)qp->peer_ctrl_seg;
	wr->wr.qword_va.target_id = qp->peer_va_ids[MLX5_QP_PEER_VA_ID_BF];
	wr->wr.qword_va.offset = 0;

	qp->peer_ctrl_seg = NULL;
	commit_ctx->entries = entries;

	return 0;
}

int mlx5_exp_rollback_send(struct ibv_qp *ibqp,
		       struct ibv_exp_rollback_ctx *rollback)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	int diff;

	if (rollback->flags & IBV_EXP_ROLLBACK_ABORT_UNCOMMITED) {
		diff = (qp->gen_data.scur_post & 0xffff)
		     - ntohl(qp->gen_data.db[MLX5_SND_DBR]);
		if (diff < 0)
			diff += 0x10000;
		qp->gen_data.scur_post -= diff;
	} else {
		if (!(rollback->flags & IBV_EXP_ROLLBACK_ABORT_LATE)) {
			if (qp->gen_data.scur_post !=
			    (rollback->rollback_id >> 32))
				return -ERANGE;
		}
		qp->gen_data.scur_post = rollback->rollback_id & 0xffffffff;
	}
	return 0;
}

int mlx5_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
		   struct ibv_send_wr **bad_wr)
{

#ifdef MW_DEBUG
	if (wr->opcode == IBV_WR_BIND_MW) {
		if (wr->bind_mw.mw->type == IBV_MW_TYPE_1)
			return EINVAL;

		if (!wr->bind_mw.bind_info.mr ||
		    !wr->bind_mw.bind_info.addr ||
		    !wr->bind_mw.bind_info.length)
			return EINVAL;

		if (wr->bind_mw.bind_info.mr->pd != wr->bind_mw.mw->pd)
			return EINVAL;
	}
#endif

	return __mlx5_post_send(ibqp, (struct ibv_exp_send_wr *)wr,
				(struct ibv_exp_send_wr **)bad_wr, 0);
}

int mlx5_exp_post_send(struct ibv_qp *ibqp, struct ibv_exp_send_wr *wr,
		       struct ibv_exp_send_wr **bad_wr)
{

#ifdef MW_DEBUG
	if (wr->exp_opcode == IBV_EXP_WR_BIND_MW)
		/* MW is upstream, the ibv_exp_send_wr layout is not supported */
		return EINVAL;
#endif
	return __mlx5_post_send(ibqp, wr, bad_wr, 1);
}

int mlx5_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
		 struct ibv_mw_bind *mw_bind)
{
	struct ibv_mw_bind_info	*bind_info = &mw_bind->bind_info;
	struct ibv_send_wr wr = {};
	struct ibv_exp_send_wr *bad_wr = NULL;
	int ret;

	if (!bind_info->mr && (bind_info->addr || bind_info->length)) {
		errno = EINVAL;
		return errno;
	}

	if (bind_info->mw_access_flags & IBV_ACCESS_ZERO_BASED) {
		errno = EINVAL;
		return errno;
	}

	if (bind_info->mr) {
		if (to_mmr(bind_info->mr)->alloc_flags & IBV_ACCESS_ZERO_BASED) {
			errno = EINVAL;
			return errno;
		}

		if (mw->pd != bind_info->mr->pd) {
			errno = EPERM;
			return errno;
		}
	}

	wr.opcode = IBV_WR_BIND_MW;
	wr.next = NULL;
	wr.wr_id = mw_bind->wr_id;
	wr.send_flags = mw_bind->send_flags;
	wr.bind_mw.bind_info = mw_bind->bind_info;
	wr.bind_mw.mw = mw;
	wr.bind_mw.rkey = ibv_inc_rkey(mw->rkey);

	ret = __mlx5_post_send(qp, (struct ibv_exp_send_wr *)(uintptr_t)(&wr), &bad_wr, 0);
	if (ret)
		return ret;

	mw->rkey = wr.bind_mw.rkey;
	return 0;
}

static void set_sig_seg(struct mlx5_qp *qp, struct mlx5_rwqe_sig *sig,
			int size, uint16_t idx)
{
	uint8_t  sign;
	uint32_t qpn = qp->verbs_qp.qp.qp_num;

	sign = calc_xor(sig + 1, size);
	sign ^= calc_xor(&qpn, 4);
	sign ^= calc_xor(&idx, 2);
	sig->signature = ~sign;
}

int mlx5_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
		   struct ibv_recv_wr **bad_wr)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	struct mlx5_wqe_data_seg *scat;
	int err = 0;
	int nreq;
	int ind;
	int i, j;
	struct mlx5_rwqe_sig *sig;
	int sigsz;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(ibqp->context)->dbg_fp;
#endif

	mlx5_lock(&qp->rq.lock);

	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(!(qp->gen_data.create_flags & IBV_EXP_QP_CREATE_IGNORE_RQ_OVERFLOW) &&
		    mlx5_wq_overflow(1, nreq, qp))) {
			errno = ENOMEM;
			err = errno;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->rq.max_gs)) {
			errno = EINVAL;
			err = errno;
			*bad_wr = wr;
			goto out;
		}

		scat = get_recv_wqe(&qp->rq, ind);
		sig = (struct mlx5_rwqe_sig *)scat;
		if (unlikely(qp->ctrl_seg.wq_sig))
			++scat;

		for (i = 0, j = 0; i < wr->num_sge; ++i) {
			if (unlikely(!wr->sg_list[i].length))
				continue;
			if (unlikely(set_data_ptr_seg(scat + j++,
				     wr->sg_list + i, qp, 0))) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "failed allocating memory for global lkey structure\n");
				errno = ENOMEM;
				err = -1;
				*bad_wr = wr;
				goto out;
			}
		}

		if (j < qp->rq.max_gs) {
			scat[j].byte_count = 0;
			scat[j].lkey       = htonl(MLX5_INVALID_LKEY);
			scat[j].addr       = 0;
		}

		if (unlikely(qp->ctrl_seg.wq_sig)) {
			sigsz = min(wr->num_sge, (1 << (qp->rq.wqe_shift - 4)) - 1);

			set_sig_seg(qp, sig, sigsz << 4, qp->rq.head +  nreq);
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

		if (likely(!((ibqp->qp_type == IBV_QPT_RAW_ETH ||
			      qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) && ibqp->state < IBV_QPS_RTR)))
			qp->gen_data.db[MLX5_RCV_DBR] = htonl(qp->rq.head & 0xffff);
	}

	mlx5_unlock(&qp->rq.lock);

	return err;
}

int mlx5_use_huge(struct ibv_context *context, const char *key)
{
	char env[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(context, key, env, sizeof(env)) &&
	    !strcmp(env, "y"))
		return 1;

	return 0;
}

void *mlx5_find_rsc(struct mlx5_context *ctx, uint32_t rsn)
{
	int tind = rsn >> MLX5_QP_TABLE_SHIFT;

	if (ctx->rsc_table[tind].refcnt)
		return ctx->rsc_table[tind].table[rsn & MLX5_QP_TABLE_MASK];
	else
		return NULL;
}

int mlx5_store_rsc(struct mlx5_context *ctx, uint32_t rsn, void *rsc)
{
	int tind = rsn >> MLX5_QP_TABLE_SHIFT;

	if (!ctx->rsc_table[tind].refcnt) {
		ctx->rsc_table[tind].table = calloc(MLX5_QP_TABLE_MASK + 1,
						    sizeof(void *));
		if (!ctx->rsc_table[tind].table)
			return -1;
	}

	++ctx->rsc_table[tind].refcnt;
	ctx->rsc_table[tind].table[rsn & MLX5_QP_TABLE_MASK] = rsc;
	return 0;
}

void mlx5_clear_rsc(struct mlx5_context *ctx, uint32_t rsn)
{
	int tind = rsn >> MLX5_QP_TABLE_SHIFT;

	if (!--ctx->rsc_table[tind].refcnt)
		free(ctx->rsc_table[tind].table);
	else
		ctx->rsc_table[tind].table[rsn & MLX5_QP_TABLE_MASK] = NULL;
}

int mlx5_post_task(struct ibv_context *context,
		   struct ibv_exp_task *task_list,
		   struct ibv_exp_task **bad_task)
{
	int rc = 0;
	struct ibv_exp_task *cur_task = NULL;
	struct ibv_exp_send_wr *bad_wr;
	struct ibv_recv_wr *bad_wr_r;
	struct mlx5_context *mlx5_ctx = to_mctx(context);

	if (!task_list)
		return rc;

	pthread_mutex_lock(&mlx5_ctx->task_mutex);

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
					&bad_wr_r);
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

	pthread_mutex_unlock(&mlx5_ctx->task_mutex);

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
 * several messages, then he can call send_flush to ring the QP DB
 *
 * This function is used to implement the following QP burst family functions:
 * - send_pending
 * - send_pending_inline
 * - send_pending_sg_list
 * - send_burst
 */

static inline int send_pending(struct ibv_qp *ibqp, uint64_t addr,
			       uint32_t length, uint32_t lkey,
			       uint32_t flags, uint16_t *vlan_tci,
			       const int use_raw_eth, const int use_inl,
			       const int thread_safe, const int use_sg_list,
			       const int use_mpw, const int use_vlan_ins,
			       const int num_sge, struct ibv_sge *sg_list) __attribute__((always_inline));
static inline int send_pending(struct ibv_qp *ibqp, uint64_t addr,
			       uint32_t length, uint32_t lkey,
			       uint32_t flags, uint16_t *vlan_tci,
			       const int use_raw_eth, const int use_inl,
			       const int thread_safe, const int use_sg_list,
			       const int use_mpw, const int use_vlan_ins,
			       const int num_sge, struct ibv_sge *sg_list)

{
	struct mlx5_wqe_inline_seg *uninitialized_var(inl_seg);
	struct mlx5_wqe_data_seg *uninitialized_var(dseg);
	uint8_t *uninitialized_var(inl_data);
	uint32_t *uninitialized_var(start);
	struct mlx5_qp *qp = to_mqp(ibqp);
	int uninitialized_var(size);
	uint8_t fm_ce_se;
	int i;

	/* mpw with vlan insertion not supported */
	if (use_mpw && use_vlan_ins)
		return EPERM;

	if (thread_safe)
		mlx5_lock(&qp->sq.lock);

	if (use_mpw) {
		uint32_t msg_size, n_sg;

		if (use_sg_list) {
			msg_size = 0;
			for (i = 0; i < num_sge; i++)
				msg_size += sg_list[i].length;
			n_sg = num_sge;
		} else {
			msg_size = length;
			n_sg = 1;
		}
		if (use_inl &&
		    (qp->mpw.state == MLX5_MPW_STATE_OPENED_INL) &&
		    (qp->mpw.len == msg_size) &&
		    ((qp->mpw.flags & ~IBV_EXP_QP_BURST_SIGNALED) ==
		     (flags & ~IBV_EXP_QP_BURST_SIGNALED)) &&
		    ((qp->mpw.total_len + msg_size) <= qp->data_seg.max_inline_data)) {
			/* Add current message to opened inline multi-packet WQE */
			inl_seg = (struct mlx5_wqe_inline_seg *)(qp->mpw.ctrl_update + 7);
			inl_data = qp->mpw.inl_data + qp->mpw.len;
			if (unlikely((void *)inl_data >= qp->gen_data.sqend))
				inl_data = (uint8_t *)mlx5_get_send_wqe(qp, 0) +
					   (inl_data - (uint8_t *)qp->gen_data.sqend);
			qp->mpw.total_len += msg_size;
		} else if (!use_inl &&
			   (qp->mpw.state == MLX5_MPW_STATE_OPENED) &&
			   (qp->mpw.len == msg_size) &&
			   ((qp->mpw.flags & ~IBV_EXP_QP_BURST_SIGNALED) ==
			    (flags & ~IBV_EXP_QP_BURST_SIGNALED)) &&
			   (qp->mpw.num_sge + n_sg) <= MLX5_MAX_MPW_SGE) {
			/* Add current message to opened multi-packet WQE */
			dseg = qp->mpw.last_dseg + 1;
			if (unlikely(dseg == qp->gen_data.sqend))
				dseg = mlx5_get_send_wqe(qp, 0);
			size = 0;
			qp->mpw.num_sge += n_sg;
		} else if (likely(use_inl || (msg_size <= MLX5_MAX_MPW_SIZE))) {
			/* Open new multi-packet WQE
			 *
			 * In case of inline the user must make sure that
			 * message size is smaller than max_inline which
			 * means that it is also smaller than MLX5_MAX_MPW_SIZE
			 * This guarantees that we can open multi-packet WQE.
			 * In case of non-inline we must check that msg_size is
			 * smaller than MLX5_MAX_MPW_SIZE.
			 */

			qp->mpw.state = MLX5_MPW_STATE_OPENING;
			qp->mpw.len = msg_size;
			qp->mpw.num_sge = n_sg;
			qp->mpw.flags = flags;
			qp->mpw.scur_post = qp->gen_data.scur_post;
			qp->mpw.total_len = msg_size;
		} else {
			/* We can't open new multi-packet WQE
			 * since msg_size > MLX5_MAX_MPW_SIZE
			 */
			qp->mpw.state = MLX5_MPW_STATE_CLOSED;
		}
	} else {
		/* Close multi-packet WQE */
		qp->mpw.state = MLX5_MPW_STATE_CLOSED;
	}

	if (use_sg_list) {
		addr = sg_list[0].addr;
		length = sg_list[0].length;
		lkey = sg_list[0].lkey;
	}

	/*
	 * Start new WQE if there is no open multi-packet WQE,
	 * or if using vlan insertion which not support multi-packet.
	 */
	if (use_vlan_ins ||
	    (use_inl && (qp->mpw.state != MLX5_MPW_STATE_OPENED_INL)) ||
	    (!use_inl && (qp->mpw.state != MLX5_MPW_STATE_OPENED))) {
		start = mlx5_get_send_wqe(qp, qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1));

		if (use_raw_eth) {
			struct mlx5_wqe_eth_seg *eseg;

			eseg = (struct mlx5_wqe_eth_seg *)(((char *)start) +
							   sizeof(struct mlx5_wqe_ctrl_seg));
			/* reset rsvd0, cs_flags, rsvd1, mss and rsvd2 fields */
			*((uint64_t *)eseg) = 0;
			eseg->rsvd2 = 0;

			if (flags & IBV_EXP_QP_BURST_IP_CSUM)
				eseg->cs_flags = MLX5_ETH_WQE_L3_CSUM | MLX5_ETH_WQE_L4_CSUM;
			if (use_mpw && (qp->mpw.state == MLX5_MPW_STATE_OPENING)) {
				eseg->mss = htons(qp->mpw.len);
				eseg->inline_hdr_sz = 0;
				size = (sizeof(struct mlx5_wqe_ctrl_seg) +
					offsetof(struct mlx5_wqe_eth_seg, inline_hdr)) / 16;
				if (use_inl) {
					inl_seg = (struct mlx5_wqe_inline_seg *)(start +
										 (size * 4));
					inl_data = (uint8_t *)(inl_seg + 1);
				} else {
					dseg = (struct mlx5_wqe_data_seg *)(start +
									    (size * 4));
				}
			} else {
				if (use_vlan_ins) {
					uint32_t vlan;

					eseg->inline_hdr_sz = htons(MLX5_ETH_VLAN_INLINE_HEADER_SIZE);

					/* We don't support header divided in several sges */
					if (unlikely(length <= MLX5_ETH_VLAN_INLINE_HEADER_SIZE - sizeof(vlan)))
						return EINVAL;

					vlan = htonl(0x81000000 | *vlan_tci);

					/*
					 * Copy 12 bytes of source & destination MAC address.
					 * Copy 4 bytes of vlan.
					 * Copy 2 bytes of ether type.
					 */
					memcpy((void *)(uintptr_t)eseg->inline_hdr_start, (void *)(uintptr_t)addr, 12);
					memcpy((void *)((uintptr_t)eseg->inline_hdr_start + 12), &vlan, sizeof(vlan));
					memcpy((void *)((uintptr_t)eseg->inline_hdr_start + 16), (void *)(uintptr_t)addr + 12, 2);

					/* We copied only 14 bytes from addr */
					addr += MLX5_ETH_VLAN_INLINE_HEADER_SIZE - sizeof(vlan);
					length -= MLX5_ETH_VLAN_INLINE_HEADER_SIZE - sizeof(vlan);
				} else {
					eseg->inline_hdr_sz = htons(MLX5_ETH_INLINE_HEADER_SIZE);

					/* We don't support header divided in several sges */
					if (unlikely(length <= MLX5_ETH_INLINE_HEADER_SIZE))
						return EINVAL;

					/* Copy the first bytes into the inline header */
					memcpy(eseg->inline_hdr_start, (void *)(uintptr_t)addr,
					       MLX5_ETH_INLINE_HEADER_SIZE);

					addr += MLX5_ETH_INLINE_HEADER_SIZE;
					length -= MLX5_ETH_INLINE_HEADER_SIZE;
				}
				size = (sizeof(struct mlx5_wqe_ctrl_seg) +
					sizeof(struct mlx5_wqe_eth_seg)) / 16;
				dseg = (struct mlx5_wqe_data_seg *)(++eseg);
			}
		} else {
			size = sizeof(struct mlx5_wqe_ctrl_seg) / 16;
			dseg = (struct mlx5_wqe_data_seg *)(((char *)start) + sizeof(struct mlx5_wqe_ctrl_seg));
		}
	}

	if (use_inl) {
		if (use_mpw) {
			if (unlikely((inl_data + qp->mpw.len) >
				     (uint8_t *)qp->gen_data.sqend)) {
				int size2end = ((uint8_t *)qp->gen_data.sqend - inl_data);

				memcpy(inl_data, (void *)(uintptr_t)addr, size2end);
				memcpy(mlx5_get_send_wqe(qp, 0),
				       (void *)(uintptr_t)(addr + size2end),
				       qp->mpw.len - size2end);

			} else {
				memcpy(inl_data, (void *)(uintptr_t)addr, qp->mpw.len);
			}
			inl_seg->byte_count = htonl(qp->mpw.total_len | MLX5_INLINE_SEG);
			size = (sizeof(struct mlx5_wqe_ctrl_seg) +
				offsetof(struct mlx5_wqe_eth_seg, inline_hdr)) / 16;
			size += align(qp->mpw.total_len + sizeof(inl_seg->byte_count), 16) / 16;
		} else {
			struct ibv_sge sg_list = {addr, length, 0};

			set_data_inl_seg(qp, 1, &sg_list, dseg, &size, 0, 0);
		}
	} else {
		size += sizeof(struct mlx5_wqe_data_seg) / 16;
		dseg->byte_count = htonl(length);
		dseg->lkey = htonl(lkey);
		dseg->addr = htonll(addr);
	}

	/* No inline when using sg list */
	if (use_sg_list) {
		for (i = 0; i < num_sge - 1; ++i) {
			sg_list++;
			if (likely(sg_list->length)) {
				dseg++;
				if (unlikely(dseg == qp->gen_data.sqend))
					dseg = mlx5_get_send_wqe(qp, 0);
				size += sizeof(struct mlx5_wqe_data_seg) / 16;
				dseg->byte_count = htonl(sg_list->length);
				dseg->lkey = htonl(sg_list->lkey);
				dseg->addr = htonll(sg_list->addr);
			}
		}
	}
	if (use_mpw) {
		if (use_inl)
			qp->mpw.inl_data = inl_data;
		else
			qp->mpw.last_dseg = dseg;
	}

	if (use_vlan_ins ||
	    (use_inl && (qp->mpw.state != MLX5_MPW_STATE_OPENED_INL)) ||
	    (!use_inl && (qp->mpw.state != MLX5_MPW_STATE_OPENED))) {
		/* Fill ctrl-segment of a new WQE */
		fm_ce_se = qp->ctrl_seg.fm_ce_se_acc[flags & (IBV_EXP_QP_BURST_SOLICITED |
							      IBV_EXP_QP_BURST_SIGNALED |
							      IBV_EXP_QP_BURST_FENCE)];
		if (unlikely(qp->gen_data.fm_cache)) {
			if (unlikely(flags & IBV_SEND_FENCE))
				fm_ce_se |= MLX5_FENCE_MODE_SMALL_AND_FENCE;
			else
				fm_ce_se |= qp->gen_data.fm_cache;
			qp->gen_data.fm_cache = 0;
		}

		if (likely(use_mpw && (qp->mpw.state == MLX5_MPW_STATE_OPENING))) {
			*start++ = htonl((MLX5_OPC_MOD_MPW << 24) |
					 ((qp->gen_data.scur_post & 0xFFFF) << 8) |
					 MLX5_OPCODE_TSO);
			qp->mpw.ctrl_update = start;
			if ((flags & IBV_EXP_QP_BURST_SIGNALED) ||
			    (qp->mpw.num_sge >= MLX5_MAX_MPW_SGE)) {
				qp->mpw.state = MLX5_MPW_STATE_CLOSED;
			} else {
				if (use_inl)
					qp->mpw.state = MLX5_MPW_STATE_OPENED_INL;
				else
					qp->mpw.state = MLX5_MPW_STATE_OPENED;
				qp->mpw.size = size;
			}
		} else {
			*start++ = htonl((qp->gen_data.scur_post & 0xFFFF) << 8 |
					 MLX5_OPCODE_SEND);
		}
		*start++ = htonl(qp->ctrl_seg.qp_num << 8 | (size & 0x3F));
		*start++ = htonl(fm_ce_se);
		*start = 0;

		qp->gen_data.wqe_head[qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1)] = ++(qp->sq.head);
		/* Update last_post to point on the position of the new WQE */
		qp->gen_data.last_post = qp->gen_data.scur_post;
		qp->gen_data.scur_post += DIV_ROUND_UP(size * 16, MLX5_SEND_WQE_BB);
	} else {
		/* Update the multi-packt WQE ctrl-segment */
		if (use_inl)
			qp->mpw.size = size;
		else
			qp->mpw.size += size;
		*qp->mpw.ctrl_update = htonl(qp->ctrl_seg.qp_num << 8 | ((qp->mpw.size) & 0x3F));
		qp->gen_data.scur_post = qp->mpw.scur_post + DIV_ROUND_UP(qp->mpw.size * 16, MLX5_SEND_WQE_BB);
		if (flags & IBV_EXP_QP_BURST_SIGNALED) {
			*(qp->mpw.ctrl_update + 1) |= htonl(MLX5_WQE_CTRL_CQ_UPDATE);
			qp->mpw.state = MLX5_MPW_STATE_CLOSED;
		} else if (unlikely(qp->mpw.num_sge == MLX5_MAX_MPW_SGE)) {
			qp->mpw.state = MLX5_MPW_STATE_CLOSED;
		}
	}

	if (thread_safe)
		mlx5_unlock(&qp->sq.lock);

	return 0;
}

/* burst family - send_pending */
static int mlx5_send_pending_safe(struct ibv_qp *qp, uint64_t addr,
				  uint32_t length, uint32_t lkey,
				  uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_pending_safe(struct ibv_qp *qp, uint64_t addr,
				  uint32_t length, uint32_t lkey,
				  uint32_t flags)
{
	struct mlx5_qp *mqp = to_mqp(qp);
	int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;

			/*  qp, addr, length, lkey, flags, vlan, raw_eth, inl, safe, */
	return send_pending(qp, addr, length, lkey, flags, NULL,     raw_eth, 0,   1,
			/*  use_sg, use_mpw, use_vlan, num_sge, sg_list	*/
			    0,      0,       0,	       0,       NULL);
}

static int mlx5_send_pending_mpw_safe(struct ibv_qp *qp, uint64_t addr,
				      uint32_t length, uint32_t lkey,
				      uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_pending_mpw_safe(struct ibv_qp *qp, uint64_t addr,
				      uint32_t length, uint32_t lkey,
				      uint32_t flags)
{
	struct mlx5_qp *mqp = to_mqp(qp);
	int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;

			/*  qp, addr, length, lkey, flags, vlan, raw_eth, inl, safe, */
	return send_pending(qp, addr, length, lkey, flags, NULL,     raw_eth, 0,   1,
			/*  use_sg, use_mpw, use_vlan, num_sge, sg_list */
			    0,      1,       0,	       0,       NULL);
}

#define MLX5_SEND_PENDING_VLAN_SAFE_NAME(mpw) mlx5_send_pending_vlan_safe_##mpw
#define MLX5_SEND_PENDING_VLAN_SAFE(mpw)							\
	static int MLX5_SEND_PENDING_VLAN_SAFE_NAME(mpw)(					\
					struct ibv_qp *qp, uint64_t addr,			\
					uint32_t length, uint32_t lkey,				\
					uint32_t flags, uint16_t *vlan_tci) __MLX5_ALGN_F__;	\
	static int MLX5_SEND_PENDING_VLAN_SAFE_NAME(mpw)(					\
					struct ibv_qp *qp, uint64_t addr,			\
					uint32_t length, uint32_t lkey,				\
					uint32_t flags, uint16_t *vlan_tci)			\
	{											\
		struct mlx5_qp *mqp = to_mqp(qp);						\
		int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&		\
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;				\
												\
		/*                  qp, addr, length, lkey, flags, vlan,     eth, */		\
		return send_pending(qp, addr, length, lkey, flags, vlan_tci, raw_eth,		\
			/*  inl, safe,  use_sg, use_mpw, use_vlan, num_sge, sg_list */		\
			    0,   1,     0,      mpw,     1,    	   0,       NULL);		\
	}
/*			    mpw */
MLX5_SEND_PENDING_VLAN_SAFE(0);
MLX5_SEND_PENDING_VLAN_SAFE(1);

#define MLX5_SEND_PENDING_UNSAFE_NAME(eth, mpw) mlx5_send_pending_unsafe_##eth##mpw
#define MLX5_SEND_PENDING_UNSAFE(eth, mpw)					\
	static int MLX5_SEND_PENDING_UNSAFE_NAME(eth, mpw)(			\
					struct ibv_qp *qp, uint64_t addr,	\
					uint32_t length, uint32_t lkey,		\
					uint32_t flags) __MLX5_ALGN_F__;	\
	static int MLX5_SEND_PENDING_UNSAFE_NAME(eth, mpw)(			\
					struct ibv_qp *qp, uint64_t addr,	\
					uint32_t length, uint32_t lkey,		\
					uint32_t flags)				\
	{									\
		/*                  qp, addr, length, lkey, flags, */		\
		return send_pending(qp, addr, length, lkey, flags, 		\
				/*  vlan, eth, inl, safe,  use_sg, */		\
				    NULL, eth, 0,   0,     0,  			\
				/*  use_mpw, use_vlan, num_sge, sg_list */	\
				    mpw,     0,	       0,       NULL);		\
	}
/*			eth mpw */
MLX5_SEND_PENDING_UNSAFE(0,  0);
MLX5_SEND_PENDING_UNSAFE(0,  1);
MLX5_SEND_PENDING_UNSAFE(1,  0);
MLX5_SEND_PENDING_UNSAFE(1,  1);

#define MLX5_SEND_PENDING_VLAN_UNSAFE_NAME(mpw) mlx5_send_pending_vlan_unsafe_##mpw
#define MLX5_SEND_PENDING_VLAN_UNSAFE(mpw)						\
	static int MLX5_SEND_PENDING_VLAN_UNSAFE_NAME(mpw)(				\
					struct ibv_qp *qp, uint64_t addr,		\
					uint32_t length, uint32_t lkey,	uint32_t flags,	\
					uint16_t *vlan_tci) __MLX5_ALGN_F__;		\
	static int MLX5_SEND_PENDING_VLAN_UNSAFE_NAME(mpw)(				\
					struct ibv_qp *qp, uint64_t addr,		\
					uint32_t length, uint32_t lkey,	uint32_t flags,	\
					uint16_t *vlan_tci)				\
	{										\
		/*                  qp, addr, length, lkey, flags, */			\
		return send_pending(qp, addr, length, lkey, flags,			\
				 /* vlan,     eth, inl, safe,  use_sg*/			\
				    vlan_tci, 1,   0,   0,     0,			\
				 /* use_mpw, use_vlan, num_sge, sg_list */		\
				    mpw,     1,	       0,       NULL);			\
	}
/*			      mpw */
MLX5_SEND_PENDING_VLAN_UNSAFE(0);
MLX5_SEND_PENDING_VLAN_UNSAFE(1);

/* burst family - send_pending_inline */
static int mlx5_send_pending_inl_safe(struct ibv_qp *qp, void *addr,
				      uint32_t length, uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_pending_inl_safe(struct ibv_qp *qp, void *addr,
				      uint32_t length, uint32_t flags)
{
	struct mlx5_qp *mqp = to_mqp(qp);
	int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;

			/*  qp, addr,            length, lkey, flags, vlan, raw_eth, */
	return send_pending(qp, (uintptr_t)addr, length, 0,    flags, NULL, raw_eth,
			/*  inl, safe,  use_sg, use_mpw, use_vlan, num_sge, sg_list	*/
			    1,   1,     0,      0,       0,	   0,       NULL);
}

static int mlx5_send_pending_inl_mpw_safe(struct ibv_qp *qp, void *addr,
					  uint32_t length, uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_pending_inl_mpw_safe(struct ibv_qp *qp, void *addr,
					  uint32_t length, uint32_t flags)
{
	struct mlx5_qp *mqp = to_mqp(qp);
	int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;

			/*  qp, addr,            length, lkey, flags, vlan, raw_eth, */
	return send_pending(qp, (uintptr_t)addr, length, 0,    flags, NULL, raw_eth,
			/*  inl, safe,  use_sg, use_mpw, use_vlan, num_sge, sg_list	*/
			    1,   1,     0,      1,       0,	   0,       NULL);
}

#define MLX5_SEND_PENDING_INL_VLAN_SAFE_NAME(mpw) mlx5_send_pending_inl_vlan_safe_##mpw
#define MLX5_SEND_PENDING_INL_VLAN_SAFE(mpw)							\
	static int MLX5_SEND_PENDING_INL_VLAN_SAFE_NAME(mpw)(					\
					struct ibv_qp *qp, void *addr, uint32_t length,		\
					uint32_t flags, uint16_t *vlan_tci) __MLX5_ALGN_F__;	\
	static int MLX5_SEND_PENDING_INL_VLAN_SAFE_NAME(mpw)(					\
					struct ibv_qp *qp, void *addr, uint32_t length,		\
					uint32_t flags, uint16_t *vlan_tci)			\
	{											\
		struct mlx5_qp *mqp = to_mqp(qp);						\
		int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&		\
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;				\
		/*                  qp, addr, 		 length, lkey, */			\
		return send_pending(qp, (uintptr_t)addr, length, 0,				\
				/*  flags, vlan,     eth,     inl, safe, use_sg, */		\
				    flags, vlan_tci, raw_eth, 1,   1,    0,			\
				/*  use_mpw, use_vlan, num_sge, sg_list */			\
				    mpw,     1,	       0,       NULL);				\
	}
/*				mpw */
MLX5_SEND_PENDING_INL_VLAN_SAFE(0);
MLX5_SEND_PENDING_INL_VLAN_SAFE(1);

#define MLX5_SEND_PENDING_INL_UNSAFE_NAME(eth, mpw) mlx5_send_pending_inl_unsafe_##eth##mpw
#define MLX5_SEND_PENDING_INL_UNSAFE(eth, mpw)							\
	static int MLX5_SEND_PENDING_INL_UNSAFE_NAME(eth, mpw)(					\
					struct ibv_qp *qp, void *addr,				\
					uint32_t length, uint32_t flags) __MLX5_ALGN_F__;	\
	static int MLX5_SEND_PENDING_INL_UNSAFE_NAME(eth, mpw)(					\
					struct ibv_qp *qp, void *addr,				\
					uint32_t length, uint32_t flags)			\
	{											\
		/*                  qp, addr,            length, lkey, flags, vlan, eth, inl, */\
		return send_pending(qp, (uintptr_t)addr, length, 0,    flags, NULL, eth, 1,	\
				/*  safe,  use_sg, use_mpw, use_vlan, num_sge, sg_list */	\
				    0,     0,      mpw,     0,	      0,       NULL);		\
	}
/*			    eth mpw */
MLX5_SEND_PENDING_INL_UNSAFE(0, 0);
MLX5_SEND_PENDING_INL_UNSAFE(0, 1);
MLX5_SEND_PENDING_INL_UNSAFE(1, 0);
MLX5_SEND_PENDING_INL_UNSAFE(1, 1);

#define MLX5_SEND_PENDING_INL_VLAN_UNSAFE_NAME(mpw) mlx5_send_pending_inl_unsafe_##mpw
#define MLX5_SEND_PENDING_INL_VLAN_UNSAFE(mpw)							\
	static int MLX5_SEND_PENDING_INL_VLAN_UNSAFE_NAME(mpw)(					\
					struct ibv_qp *qp, void *addr, uint32_t length,		\
					uint32_t flags, uint16_t *vlan_tci) __MLX5_ALGN_F__;	\
	static int MLX5_SEND_PENDING_INL_VLAN_UNSAFE_NAME(mpw)(					\
					struct ibv_qp *qp, void *addr, uint32_t length,		\
					uint32_t flags, uint16_t *vlan_tci)			\
	{											\
		/*                  qp, addr,            length, lkey, flags, */		\
		return send_pending(qp, (uintptr_t)addr, length, 0,    flags,			\
				/*  vlan,     eth, inl, safe, use_sg, */			\
				    vlan_tci, 1,   1,	0,    0,				\
				/*  use_mpw, use_vlan, num_sge, sg_list */			\
				    mpw,     1,	       0,       NULL);				\
	}
/*			    	  mpw */
MLX5_SEND_PENDING_INL_VLAN_UNSAFE(0);
MLX5_SEND_PENDING_INL_VLAN_UNSAFE(1);

/* burst family - send_pending_sg_list */
static int mlx5_send_pending_sg_list_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_pending_sg_list_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags)
{
	struct mlx5_qp *mqp = to_mqp(ibqp);
	int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET && mqp->link_layer == IBV_LINK_LAYER_ETHERNET;

			/*  qp,   addr, length, lkey, flags, vlan, raw_eth, inl,	*/
	return send_pending(ibqp, 0,    0,      0,    flags, NULL, raw_eth, 0,
			/*  safe,  use_sg, use_mpw, use_vlan, num_sge, sg_list */
			    1,     1,      0,       0,	      num,     sg_list);
}

static int mlx5_send_pending_sg_list_mpw_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_pending_sg_list_mpw_safe(
		struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
		uint32_t flags)
{
	struct mlx5_qp *mqp = to_mqp(ibqp);
	int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET && mqp->link_layer == IBV_LINK_LAYER_ETHERNET;

			/*  qp,   addr, length, lkey, flags, vlan, raw_eth, inl, */
	return send_pending(ibqp, 0,    0,      0,    flags, NULL,     raw_eth, 0,
			/*  safe,  use_sg, use_mpw, use_vlan, num_sge, sg_list */
			    1,     1,      1,       0,	      num,     sg_list);
}

#define MLX5_SEND_PENDING_SG_VLAN_SAFE_NAME(mpw) mlx5_send_pending_sg_list_vlan_safe_##mpw
#define MLX5_SEND_PENDING_SG_VLAN_SAFE(mpw)							\
	static int MLX5_SEND_PENDING_SG_VLAN_SAFE_NAME(mpw)(					\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags,				\
					uint16_t *vlan_tci) __MLX5_ALGN_F__;			\
	static int MLX5_SEND_PENDING_SG_VLAN_SAFE_NAME(mpw)(					\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags,				\
					uint16_t *vlan_tci)					\
	{											\
		struct mlx5_qp *mqp = to_mqp(ibqp);						\
		int raw_eth = mqp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&		\
		      mqp->link_layer == IBV_LINK_LAYER_ETHERNET;				\
												\
		/*                  qp,   addr, length, lkey, */				\
		return send_pending(ibqp, 0, 	0, 	0,					\
				/*  flags, vlan,     eth,     inl, safe, use_sg, */		\
				    flags, vlan_tci, raw_eth, 0,   1,    1,			\
				/*  use_mpw, use_vlan, num_sge, sg_list */			\
				    mpw,     1,	       num,     sg_list);			\
	}
/*				mpw */
MLX5_SEND_PENDING_SG_VLAN_SAFE(0);
MLX5_SEND_PENDING_SG_VLAN_SAFE(1);

#define MLX5_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, mpw) mlx5_send_pending_sg_list_unsafe_##eth##mpw
#define MLX5_SEND_PENDING_SG_LIST_UNSAFE(eth, mpw)						\
	static int MLX5_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, mpw)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags) __MLX5_ALGN_F__;		\
	static int MLX5_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, mpw)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags)				\
	{											\
				/*  qp,   addr, length, lkey, flags, vlan, eth, inl, */		\
		return send_pending(ibqp, 0,    0,      0,    flags, NULL,     eth, 0,		\
				/*  safe,  use_sg, use_mpw, use_vlan, num_sge, sg_list */	\
				    0,     1,      mpw,     0,	      num,     sg_list);	\
	}
/*				eth mpw */
MLX5_SEND_PENDING_SG_LIST_UNSAFE(0,  0);
MLX5_SEND_PENDING_SG_LIST_UNSAFE(0,  1);
MLX5_SEND_PENDING_SG_LIST_UNSAFE(1,  0);
MLX5_SEND_PENDING_SG_LIST_UNSAFE(1,  1);

#define MLX5_SEND_PENDING_SG_VLAN_UNSAFE_NAME(mpw) mlx5_send_pending_sg_list_unsafe_##mpw
#define MLX5_SEND_PENDING_SG_VLAN_UNSAFE(mpw)						\
	static int MLX5_SEND_PENDING_SG_VLAN_UNSAFE_NAME(mpw)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,	\
					uint32_t num, uint32_t flags,			\
					uint16_t *vlan_tci) __MLX5_ALGN_F__;		\
	static int MLX5_SEND_PENDING_SG_VLAN_UNSAFE_NAME(mpw)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,	\
					uint32_t num, uint32_t flags,			\
					uint16_t *vlan_tci)				\
	{										\
				/*  qp,   addr, length, lkey, flags, */			\
		return send_pending(ibqp, 0,    0,      0,    flags,			\
				/*  vlan, eth, inl, safe,  use_sg, */			\
				    vlan_tci, 1,   0,	0,     1,			\
				/*  use_mpw, use_vlan, num_sge, sg_list */		\
				    mpw,     1,	       num,     sg_list);		\
	}
/*				 mpw */
MLX5_SEND_PENDING_SG_VLAN_UNSAFE(0);
MLX5_SEND_PENDING_SG_VLAN_UNSAFE(1);

/* burst family - send_burst */
static inline int send_flush_unsafe(struct ibv_qp *ibqp, const int db_method) __attribute__((always_inline));

static inline int send_msg_list(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
				uint32_t flags, const int raw_eth, const int thread_safe,
				const int db_method, const int mpw) __attribute__((always_inline));
static inline int send_msg_list(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num,
				uint32_t flags, const int raw_eth, const int thread_safe,
				const int db_method, const int mpw)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	int i;

	if (thread_safe)
		mlx5_lock(&qp->sq.lock);

	for (i = 0; i < num; i++, sg_list++)
			/*   qp,   addr,          length,          lkey,	*/
		send_pending(ibqp, sg_list->addr, sg_list->length, sg_list->lkey,
			/*   flags, vlan, raw_eth, inl, safe,  use_sg,	*/
			     flags, NULL, raw_eth, 0,   0,     0,
			/*   use_mpw, use_vlan, num_sge, sg_list	*/
			     mpw,     0,	0,       NULL);

	/* use send_flush_unsafe since lock is already taken if needed */
	send_flush_unsafe(ibqp, db_method);

	if (thread_safe)
		mlx5_unlock(&qp->sq.lock);

	return 0;
}

static int mlx5_send_burst_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_burst_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	int eth = qp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&
		  qp->link_layer == IBV_LINK_LAYER_ETHERNET;

	return send_msg_list(ibqp, sg_list, num, flags, eth, 1, qp->gen_data.bf->db_method, 0);
}

static int mlx5_send_burst_mpw_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags) __MLX5_ALGN_F__;
static int mlx5_send_burst_mpw_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num, uint32_t flags)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	int eth = qp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&
		  qp->link_layer == IBV_LINK_LAYER_ETHERNET;

	return send_msg_list(ibqp, sg_list, num, flags, eth, 1, qp->gen_data.bf->db_method, 1);
}

#define MLX5_SEND_BURST_UNSAFE_NAME(db_method, eth, mpw) mlx5_send_burst_unsafe_##db_method##eth##mpw
#define MLX5_SEND_BURST_UNSAFE(db_method, eth, mpw)						\
	static int MLX5_SEND_BURST_UNSAFE_NAME(db_method, eth, mpw)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags) __MLX5_ALGN_F__;		\
	static int MLX5_SEND_BURST_UNSAFE_NAME(db_method, eth, mpw)(				\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,		\
					uint32_t num, uint32_t flags)				\
	{											\
		return send_msg_list(ibqp, sg_list, num, flags, eth, 0, db_method, mpw);	\
	}
/*		       db_method,				eth mpw */
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF_1_THREAD,	0,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF_1_THREAD,	0,  1);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF_1_THREAD,	1,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF_1_THREAD,	1,  1);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF,			0,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF,			0,  1);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF,			1,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DEDIC_BF,			1,  1);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_BF,			0,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_BF,			0,  1);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_BF,			1,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_BF,			1,  1);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DB,			0,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DB,			0,  1);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DB,			1,  0);
MLX5_SEND_BURST_UNSAFE(MLX5_DB_METHOD_DB,			1,  1);

/* burst family - send_flush */
static inline int send_flush_unsafe(struct ibv_qp *ibqp, const int db_method)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	uint32_t curr_post = qp->gen_data.scur_post & 0xffff;
	int size = ((int)curr_post - (int)qp->gen_data.last_post + (int)0x10000) & 0xffff;
	unsigned long long *seg = mlx5_get_send_wqe(qp, qp->gen_data.last_post & (qp->sq.wqe_cnt - 1));

	return __ring_db(qp, db_method, curr_post, seg, size);
}

static int mlx5_send_flush_safe(struct ibv_qp *ibqp) __MLX5_ALGN_F__;
static int mlx5_send_flush_safe(struct ibv_qp *ibqp)
{
	struct mlx5_qp *qp = to_mqp(ibqp);

	mlx5_lock(&qp->sq.lock);
	send_flush_unsafe(ibqp, qp->gen_data.bf->db_method);
	mlx5_unlock(&qp->sq.lock);

	return 0;
}

#define MLX5_SEND_FLUSH_UNSAFE_NAME(db_method) mlx5_send_flush_unsafe_##db_method
#define MLX5_SEND_FLUSH_UNSAFE(db_method)					\
	static int MLX5_SEND_FLUSH_UNSAFE_NAME(db_method)(			\
					struct ibv_qp *ibqp) __MLX5_ALGN_F__;	\
	static int MLX5_SEND_FLUSH_UNSAFE_NAME(db_method)(			\
					struct ibv_qp *ibqp)			\
	{									\
		return send_flush_unsafe(ibqp, db_method);			\
	}
/*			db_method */
MLX5_SEND_FLUSH_UNSAFE(MLX5_DB_METHOD_DEDIC_BF_1_THREAD);
MLX5_SEND_FLUSH_UNSAFE(MLX5_DB_METHOD_DEDIC_BF);
MLX5_SEND_FLUSH_UNSAFE(MLX5_DB_METHOD_BF);
MLX5_SEND_FLUSH_UNSAFE(MLX5_DB_METHOD_DB);

/* burst family - recv_pending_sg_list */
static inline int recv_sg_list(struct mlx5_wq *rq, struct ibv_sge *sg_list, uint32_t num_sg,
			       const int thread_safe)  __attribute__((always_inline));
static inline int recv_sg_list(struct mlx5_wq *rq, struct ibv_sge *sg_list, uint32_t num_sg,
			       const int thread_safe)
{
	struct mlx5_wqe_data_seg *scat;
	unsigned int ind;
	int i, j;

	if (thread_safe)
		mlx5_lock(&rq->lock);

	ind = rq->head & (rq->wqe_cnt - 1);
	scat = get_recv_wqe(rq, ind);

	for (i = 0, j = 0; i < num_sg; ++i, sg_list++) {
		if (unlikely(!sg_list->length))
			continue;
		scat->byte_count = htonl(sg_list->length);
		scat->lkey       = htonl(sg_list->lkey);
		scat->addr       = htonll(sg_list->addr);
		scat++;
		j++;
	}
	if (j < rq->max_gs) {
		scat->byte_count = 0;
		scat->lkey       = htonl(MLX5_INVALID_LKEY);
		scat->addr       = 0;
	}
	rq->head++;

	/*
	 * Make sure that descriptors are written before
	 * doorbell record.
	 */
	wmb();

	*rq->db = htonl(rq->head & 0xffff);

	if (thread_safe)
		mlx5_unlock(&rq->lock);

	return 0;
}

/* burst family - recv_burst */
static inline int recv_burst(struct mlx5_wq *rq, struct ibv_sge *sg_list, uint32_t num,
			     const int thread_safe, const int max_one_sge, const int mp_rq)  __attribute__((always_inline));
static inline int recv_burst(struct mlx5_wq *rq, struct ibv_sge *sg_list, uint32_t num,
			     const int thread_safe, const int max_one_sge, const int mp_rq)
{
	struct mlx5_wqe_data_seg *scat;
	unsigned int ind;
	int i;

	if (thread_safe)
		mlx5_lock(&rq->lock);

	ind = rq->head & (rq->wqe_cnt - 1);
	for (i = 0; i < num; ++i) {
		scat = get_recv_wqe(rq, ind);
		/* Multi-Packet RQ WQE format is like SRQ format and requires
		 * a next-segment octword.
		 * This next-segment octword is reserved (therefore cleared)
		 * when we use CYCLIC_STRIDING_RQ
		 */
		if (mp_rq) {
			memset(scat, 0, sizeof(struct mlx5_wqe_srq_next_seg));
			scat++;
		}
		scat->byte_count = htonl(sg_list->length);
		scat->lkey       = htonl(sg_list->lkey);
		scat->addr       = htonll(sg_list->addr);

		if (!max_one_sge) {
			scat[1].byte_count = 0;
			scat[1].lkey       = htonl(MLX5_INVALID_LKEY);
			scat[1].addr       = 0;
		}

		sg_list++;
		ind = (ind + 1) & (rq->wqe_cnt - 1);
	}
	rq->head += num;

	/*
	 * Make sure that descriptors are written before
	 * doorbell record.
	 */
	wmb();

	*rq->db = htonl(rq->head & 0xffff);

	if (thread_safe)
		mlx5_unlock(&rq->lock);

	return 0;
}

static int mlx5_recv_burst_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num) __MLX5_ALGN_F__;
static int mlx5_recv_burst_safe(struct ibv_qp *ibqp, struct ibv_sge *sg_list, uint32_t num)
{
	struct mlx5_qp *qp = to_mqp(ibqp);

	return recv_burst(&qp->rq, sg_list, num, 1, qp->rq.max_gs == 1, 0);
}

#define MLX5_RECV_BURST_UNSAFE_NAME(_1sge) mlx5_recv_burst_unsafe_##_1sge
#define MLX5_RECV_BURST_UNSAFE(_1sge)							\
	static int MLX5_RECV_BURST_UNSAFE_NAME(_1sge)(					\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,	\
					uint32_t num) __MLX5_ALGN_F__;			\
	static int MLX5_RECV_BURST_UNSAFE_NAME(_1sge)(					\
					struct ibv_qp *ibqp, struct ibv_sge *sg_list,	\
					uint32_t num)					\
	{										\
		return recv_burst(&to_mqp(ibqp)->rq, sg_list, num, 0, _1sge, 0);	\
	}
/*		       _1sge */
MLX5_RECV_BURST_UNSAFE(0);
MLX5_RECV_BURST_UNSAFE(1);

/*
 * qp_burst family implementation for safe QP
 */
struct ibv_exp_qp_burst_family_v1 mlx5_qp_burst_family_safe = {
		.send_burst = mlx5_send_burst_safe,
		.send_pending = mlx5_send_pending_safe,
		.send_pending_inline = mlx5_send_pending_inl_safe,
		.send_pending_sg_list = mlx5_send_pending_sg_list_safe,
		.send_flush = mlx5_send_flush_safe,
		.recv_burst = mlx5_recv_burst_safe,
		.send_pending_vlan = MLX5_SEND_PENDING_VLAN_SAFE_NAME(0),
		.send_pending_inline_vlan = MLX5_SEND_PENDING_INL_VLAN_SAFE_NAME(0),
		.send_pending_sg_list_vlan = MLX5_SEND_PENDING_SG_VLAN_SAFE_NAME(0)
};

struct ibv_exp_qp_burst_family_v1 mlx5_qp_burst_family_mpw_safe = {
		.send_burst = mlx5_send_burst_mpw_safe,
		.send_pending = mlx5_send_pending_mpw_safe,
		.send_pending_inline = mlx5_send_pending_inl_mpw_safe,
		.send_pending_sg_list = mlx5_send_pending_sg_list_mpw_safe,
		.send_flush = mlx5_send_flush_safe,
		.recv_burst = mlx5_recv_burst_safe,
		.send_pending_vlan = MLX5_SEND_PENDING_VLAN_SAFE_NAME(1),
		.send_pending_inline_vlan = MLX5_SEND_PENDING_INL_VLAN_SAFE_NAME(1),
		.send_pending_sg_list_vlan = MLX5_SEND_PENDING_SG_VLAN_SAFE_NAME(1)
};

/*
 * qp_burst family implementation table for unsafe QP
 *
 * Each table entry contains an implementation of the ibv_exp_qp_burst_family
 * which fits to QPs with specific attributes:
 *   - db_method (MLX5_DB_METHOD_DEDIC_BF_1_THREAD, MLX5_DB_METHOD_DEDIC_BF,
 *                MLX5_DB_METHOD_BF or MLX5_DB_METHOD_DB)
 *   - raw_eth_qp (yes/no),
 *   - max-rcv-gs == 1 (yes/no)
 *
 * To get the right qp_burst_family implementation for specific QP use the QP
 * attributes (db_method << 2 | eth << 1 | _1sge) as an index for the qp_burst
 * family table
 */
#define MLX5_QP_BURST_UNSAFE_TBL_IDX(db_method, eth, _1sge, mpw)	\
		(db_method << 3 | eth << 2 | _1sge << 1 | mpw)

#define MLX5_QP_BURST_UNSAFE_TBL_ENTRY(db_method, eth, _1sge, mpw)				\
	[MLX5_QP_BURST_UNSAFE_TBL_IDX(db_method, eth, _1sge, mpw)] = {				\
		.send_burst		= MLX5_SEND_BURST_UNSAFE_NAME(db_method, eth, mpw),	\
		.send_pending		= MLX5_SEND_PENDING_UNSAFE_NAME(eth, mpw),		\
		.send_pending_inline	= MLX5_SEND_PENDING_INL_UNSAFE_NAME(eth, mpw),		\
		.send_pending_sg_list	= MLX5_SEND_PENDING_SG_LIST_UNSAFE_NAME(eth, mpw),	\
		.send_flush		= MLX5_SEND_FLUSH_UNSAFE_NAME(db_method),		\
		.recv_burst		= MLX5_RECV_BURST_UNSAFE_NAME(_1sge),			\
		.send_pending_vlan 	= MLX5_SEND_PENDING_VLAN_UNSAFE_NAME(mpw),		\
		.send_pending_inline_vlan  = MLX5_SEND_PENDING_INL_VLAN_UNSAFE_NAME(mpw),	\
		.send_pending_sg_list_vlan = MLX5_SEND_PENDING_SG_VLAN_UNSAFE_NAME(mpw),	\
	}
static struct ibv_exp_qp_burst_family_v1 mlx5_qp_burst_family_unsafe_tbl[1 << 5] = {
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 0, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 0, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 0, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 0, 1, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 1, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 1, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 1, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF_1_THREAD, 1, 1, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          0, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          0, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          0, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          0, 1, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          1, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          1, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          1, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DEDIC_BF,          1, 1, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                0, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                0, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                0, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                0, 1, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                1, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                1, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                1, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_BF,                1, 1, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                0, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                0, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                0, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                0, 1, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                1, 0, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                1, 0, 1),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                1, 1, 0),
		MLX5_QP_BURST_UNSAFE_TBL_ENTRY(MLX5_DB_METHOD_DB,                1, 1, 1),
};

struct ibv_exp_qp_burst_family_v1 *mlx5_get_qp_burst_family(struct mlx5_qp *qp,
							 struct ibv_exp_query_intf_params *params,
							 enum ibv_exp_query_intf_status *status)
{
	enum ibv_exp_query_intf_status ret = IBV_EXP_INTF_STAT_OK;
	struct ibv_exp_qp_burst_family_v1 *family = NULL;
	uint32_t unsupported_f;
	int mpw;

	if (params->intf_version > MLX5_MAX_QP_BURST_FAMILY_VER) {
		*status = IBV_EXP_INTF_STAT_VERSION_NOT_SUPPORTED;

		return NULL;
	}

	if ((qp->verbs_qp.qp.state < IBV_QPS_INIT) || (qp->verbs_qp.qp.state > IBV_QPS_RTS)) {
			*status = IBV_EXP_INTF_STAT_INVAL_OBJ_STATE;
			return NULL;
	}
	if (qp->gen_data.create_flags & IBV_EXP_QP_CREATE_MANAGED_SEND) {
		fprintf(stderr, PFX "Can't use QP burst family while QP_CREATE_MANAGED_SEND is set\n");
		*status = IBV_EXP_INTF_STAT_INVAL_PARARM;
		return NULL;
	}
	if (params->flags) {
		fprintf(stderr, PFX "Global interface flags(0x%x) are not supported for QP family\n", params->flags);
		*status = IBV_EXP_INTF_STAT_FLAGS_NOT_SUPPORTED;

		return NULL;
	}
	unsupported_f = params->family_flags & ~(IBV_EXP_QP_BURST_CREATE_ENABLE_MULTI_PACKET_SEND_WR);
	if (unsupported_f) {
		fprintf(stderr, PFX "Family flags(0x%x) are not supported for QP family\n", unsupported_f);
		*status = IBV_EXP_INTF_STAT_FAMILY_FLAGS_NOT_SUPPORTED;

		return NULL;
	}

	switch (qp->gen_data_warm.qp_type) {
	case IBV_QPT_RC:
	case IBV_QPT_UC:
	case IBV_QPT_RAW_PACKET:
		mpw = (params->family_flags & IBV_EXP_QP_BURST_CREATE_ENABLE_MULTI_PACKET_SEND_WR) &&
		      (qp->gen_data.model_flags & MLX5_QP_MODEL_MULTI_PACKET_WQE);

		if (qp->gen_data.model_flags & MLX5_QP_MODEL_FLAG_THREAD_SAFE) {
			if (mpw)
				family = &mlx5_qp_burst_family_mpw_safe;
			else
				family = &mlx5_qp_burst_family_safe;
		} else {
			int eth = qp->gen_data_warm.qp_type == IBV_QPT_RAW_PACKET &&
				  qp->link_layer == IBV_LINK_LAYER_ETHERNET;
			int _1sge = qp->rq.max_gs == 1;
			int db_method = qp->gen_data.bf->db_method;

			family = &mlx5_qp_burst_family_unsafe_tbl
					[MLX5_QP_BURST_UNSAFE_TBL_IDX(db_method, eth, _1sge, mpw)];
		}
		break;

	default:
		ret = IBV_EXP_INTF_STAT_INVAL_PARARM;
		break;
	}

	*status = ret;

	return family;
}

/*
 * WQ family
 */

/* wq family - recv_burst */
static int mlx5_wq_recv_burst_safe(struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list, uint32_t num) __MLX5_ALGN_F__;
static int mlx5_wq_recv_burst_safe(struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list, uint32_t num)
{
	struct mlx5_rwq *rwq = to_mrwq(ibwq);

	return recv_burst(&rwq->rq, sg_list, num, 1, rwq->rq.max_gs == 1, rwq->rsc.type == MLX5_RSC_TYPE_MP_RWQ);
}

#define MLX5_WQ_RECV_BURST_UNSAFE_NAME(_1sge) mlx5_wq_recv_burst_unsafe_##_1sge
#define MLX5_WQ_RECV_BURST_UNSAFE(_1sge)						\
	static int MLX5_WQ_RECV_BURST_UNSAFE_NAME(_1sge)(				\
				struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list,	\
				uint32_t num) __MLX5_ALGN_F__;				\
	static int MLX5_WQ_RECV_BURST_UNSAFE_NAME(_1sge)(				\
				struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list,	\
				uint32_t num)						\
	{										\
		struct mlx5_rwq *rwq = to_mrwq(ibwq);					\
											\
		return recv_burst(&rwq->rq, sg_list, num, 0, _1sge,			\
				  rwq->rsc.type == MLX5_RSC_TYPE_MP_RWQ);		\
	}
/*		       _1sge */
MLX5_WQ_RECV_BURST_UNSAFE(0);
MLX5_WQ_RECV_BURST_UNSAFE(1);

/* wq family - recv_sg_list */
static int mlx5_wq_recv_sg_list_safe(struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list, uint32_t num_sg) __MLX5_ALGN_F__;
static int mlx5_wq_recv_sg_list_safe(struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list, uint32_t num_sg)
{
	return recv_sg_list(&to_mrwq(ibwq)->rq, sg_list, num_sg, 1);
}

static int mlx5_wq_recv_sg_list_unsafe(struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list, uint32_t num_sg) __MLX5_ALGN_F__;
static int mlx5_wq_recv_sg_list_unsafe(struct ibv_exp_wq *ibwq, struct ibv_sge *sg_list, uint32_t num_sg)
{
	return recv_sg_list(&to_mrwq(ibwq)->rq, sg_list, num_sg, 0);
}

/*
 * wq family implementation for safe WQ
 */
struct ibv_exp_wq_family mlx5_wq_family_safe = {
	.recv_sg_list	= mlx5_wq_recv_sg_list_safe,
	.recv_burst	= mlx5_wq_recv_burst_safe
};

/*
 * wq family implementation table for unsafe WQ
 *
 * Each table entry contains an implementation of the ibv_exp_wq_family
 * which fits to WQs with specific attributes:
 *   - max-rcv-gs == 1 (yes/no)
 *
 * To get the right wq_family implementation for specific WQ use the WQ
 * attribute (_1sge) as an index for the qp_burst family table
 */
#define MLX5_WQ_UNSAFE_TBL_IDX(_1sge)	\
		(_1sge)

#define MLX5_WQ_UNSAFE_TBL_ENTRY(_1sge)						\
	[MLX5_WQ_UNSAFE_TBL_IDX(_1sge)] = {					\
		.recv_sg_list		= mlx5_wq_recv_sg_list_unsafe,		\
		.recv_burst		= MLX5_WQ_RECV_BURST_UNSAFE_NAME(_1sge)	\
	}

static struct ibv_exp_wq_family mlx5_wq_family_unsafe_tbl[1 << 1] = {
	MLX5_WQ_UNSAFE_TBL_ENTRY(0),
	MLX5_WQ_UNSAFE_TBL_ENTRY(1),
};

struct ibv_exp_wq_family *mlx5_get_wq_family(struct mlx5_rwq *rwq,
					     struct ibv_exp_query_intf_params *params,
					     enum ibv_exp_query_intf_status *status)
{
	enum ibv_exp_query_intf_status ret = IBV_EXP_INTF_STAT_OK;
	struct ibv_exp_wq_family *family = NULL;

	if (params->intf_version > MLX5_MAX_WQ_FAMILY_VER) {
		*status = IBV_EXP_INTF_STAT_VERSION_NOT_SUPPORTED;

		return NULL;
	}

	if (params->flags) {
		fprintf(stderr, PFX "Global interface flags(0x%x) are not supported for WQ family\n", params->flags);
		*status = IBV_EXP_INTF_STAT_FLAGS_NOT_SUPPORTED;

		return NULL;
	}
	if (params->family_flags) {
		fprintf(stderr, PFX "Family flags(0x%x) are not supported for WQ family\n", params->family_flags);
		*status = IBV_EXP_INTF_STAT_FAMILY_FLAGS_NOT_SUPPORTED;

		return NULL;
	}

	if (rwq->model_flags & MLX5_WQ_MODEL_FLAG_THREAD_SAFE) {
		family = &mlx5_wq_family_safe;
	} else {
		int _1sge = rwq->rq.max_gs == 1;

		family = &mlx5_wq_family_unsafe_tbl
				[MLX5_WQ_UNSAFE_TBL_IDX(_1sge)];
	}

	*status = ret;

	return family;
}

static void mlx5_tm_add_op(struct mlx5_srq *srq, struct mlx5_tag_entry *tag,
			   uint64_t wr_id, int nreq)
{
	struct mlx5_qp *qp = srq->cmd_qp;
	struct mlx5_srq_op *op;

	op = srq->op + (srq->op_tail++ & (qp->sq.wqe_cnt - 1));
	op->tag = tag;
	op->wr_id = wr_id;
	op->wqe_head = qp->sq.head + nreq + 1;
	if (tag)
		tag->expect_cqe++;
}

int mlx5_exp_post_srq_ops(struct ibv_srq *ibsrq, struct ibv_exp_ops_wr *wr,
			  struct ibv_exp_ops_wr **bad_wr)
{
	struct mlx5_srq *srq = to_msrq(ibsrq);
	struct mlx5_wqe_ctrl_seg *ctrl = NULL;
	void *uninitialized_var(wqe2ring);
	struct mlx5_tag_entry *tag;
	struct mlx5_qp *qp;
	unsigned int idx;
	int size = 0;
	int nreq = 0;
	int err = 0;
	void *qend;
	void *seg;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(ibsrq->context)->dbg_fp;
#endif

	if (unlikely(!srq->cmd_qp)) {
		*bad_wr = wr;
		return EINVAL;
	}

	mlx5_spin_lock(&srq->lock);
	qp = srq->cmd_qp;
	qend = qp->gen_data.sqend;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(mlx5_wq_overflow(0, nreq, qp))) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "work queue overflow\n");
			err = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		idx = qp->gen_data.scur_post & (qp->sq.wqe_cnt - 1);
		ctrl = seg = mlx5_get_send_wqe(qp, idx);
		seg += sizeof(*ctrl);
		size = sizeof(*ctrl) / 16;

		switch (wr->opcode) {
		case IBV_EXP_WR_TAG_ADD:
			if (unlikely(wr->tm.add.num_sge != 1 ||
				     !wr->tm.add.sg_list[0].length)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "num_sge must be 1\n");
				err = EINVAL;
				*bad_wr = wr;
				goto out;
			}
			if (unlikely(!srq->tm_head->next)) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "tag matching list is full\n");
				err = ENOMEM;
				*bad_wr = wr;
				goto out;
			}
			tag = srq->tm_head;
#ifdef MLX5_DEBUG
			if (tag->expect_cqe) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "tag matching list is corrupted\n");
				err = ENOMEM;
				*bad_wr = wr;
				goto out;
			}
#endif
			srq->tm_head = tag->next;

			set_tm_seg(seg, MLX5_TM_OPCODE_APPEND, wr,
				   tag->next - srq->tm_list);
			tag->next = NULL;
			tag->wr_id = wr->tm.add.recv_wr_id;
			if (wr->flags & IBV_EXP_OPS_TM_SYNC)
				srq->unexp_out = wr->tm.unexpected_cnt;
			tag->phase_cnt = srq->unexp_out;
			tag->expect_cqe++;
			tag->ptr = (void *)(uintptr_t)wr->tm.add.sg_list->addr;
			tag->size = wr->tm.add.sg_list->length;

			if (wr->flags & IBV_EXP_OPS_SIGNALED)
				mlx5_tm_add_op(srq, tag, wr->wr_id, nreq);

			wr->tm.handle = tag - srq->tm_list;
			seg += sizeof(struct mlx5_wqe_tm_seg);
			size += sizeof(struct mlx5_wqe_tm_seg) / 16;

			if (unlikely(seg == qend))
				seg = mlx5_get_send_wqe(qp, 0);

			set_data_ptr_seg(seg, wr->tm.add.sg_list, qp, 0);
			size += sizeof(struct mlx5_wqe_data_seg) / 16;
			break;

		case IBV_EXP_WR_TAG_DEL:
			tag = &srq->tm_list[wr->tm.handle];

#ifdef MLX5_DEBUG
			if (!tag->expect_cqe) {
				mlx5_dbg(fp, MLX5_DBG_QP_SEND, "removing tag which wasn't added\n");
				err = ENOMEM;
				*bad_wr = wr;
				goto out;
			}
#endif
			set_tm_seg(seg, MLX5_TM_OPCODE_REMOVE, wr,
				   wr->tm.handle);

			if (wr->flags & IBV_EXP_OPS_SIGNALED)
				mlx5_tm_add_op(srq, tag, wr->wr_id, nreq);
			else
				mlx5_tm_release_tag(srq, tag);

			seg += sizeof(struct mlx5_wqe_tm_seg);
			size += sizeof(struct mlx5_wqe_tm_seg) / 16;
			break;

		case IBV_EXP_WR_TAG_SYNC:
			set_tm_seg(seg, MLX5_TM_OPCODE_NOP, wr, 0);

			if (wr->flags & IBV_EXP_OPS_SIGNALED)
				mlx5_tm_add_op(srq, NULL, wr->wr_id, nreq);

			seg += sizeof(struct mlx5_wqe_tm_seg);
			size += sizeof(struct mlx5_wqe_tm_seg) / 16;
			break;

		default:
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "bad opcode %d\n",
				 wr->opcode);
			err = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		set_ctrl_seg((uint32_t *)ctrl, &qp->ctrl_seg,
		     MLX5_OPCODE_TAG_MATCHING, qp->gen_data.scur_post,
		     0, size, 0, 0);

		qp->gen_data.scur_post += DIV_ROUND_UP(size * 16, MLX5_SEND_WQE_BB);
		wqe2ring = ctrl;

#ifdef MLX5_DEBUG
		if (mlx5_debug_mask & MLX5_DBG_QP_SEND)
			dump_wqe(fp, idx, size, qp);
#endif
	}

out:
	if (likely(nreq)) {
		qp->sq.head += nreq;
		__ring_db(qp, qp->gen_data.bf->db_method, qp->gen_data.scur_post & 0xffff, wqe2ring, (size + 3) / 4);
	}

	mlx5_spin_unlock(&srq->lock);

	return err;
}

