/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems.  All rights reserved.
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

#include <infiniband/opcode.h>

#include "mlx4.h"
#include "doorbell.h"

int mlx4_stall_num_loop = 300;

enum {
	MLX4_CQ_DOORBELL			= 0x20
};

enum {
	CQ_OK					=  0,
	CQ_EMPTY				= -1,
	CQ_POLL_ERR				= -2
};

#define MLX4_CQ_DB_REQ_NOT_SOL			(1 << 24)
#define MLX4_CQ_DB_REQ_NOT			(2 << 24)

enum {
	MLX4_CQE_L2_TUNNEL_IPV4			= 1 << 25,
	MLX4_CQE_L2_TUNNEL_L4_CSUM		= 1 << 26,
	MLX4_CQE_L2_TUNNEL			= 1 << 27,
	MLX4_CQE_VLAN_PRESENT_MASK		= 1 << 29,
	MLX4_CQE_L2_TUNNEL_IPOK			= 1 << 31,
	MLX4_CQE_QPN_MASK			= 0xffffff,
};

enum {
	MLX4_CQE_OWNER_MASK			= 0x80,
	MLX4_CQE_IS_SEND_MASK			= 0x40,
	MLX4_CQE_INL_SCATTER_MASK		= 0x20,
	MLX4_CQE_OPCODE_MASK			= 0x1f
};

enum {
	MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR		= 0x01,
	MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR		= 0x02,
	MLX4_CQE_SYNDROME_LOCAL_PROT_ERR		= 0x04,
	MLX4_CQE_SYNDROME_WR_FLUSH_ERR			= 0x05,
	MLX4_CQE_SYNDROME_MW_BIND_ERR			= 0x06,
	MLX4_CQE_SYNDROME_BAD_RESP_ERR			= 0x10,
	MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR		= 0x11,
	MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR		= 0x12,
	MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR		= 0x13,
	MLX4_CQE_SYNDROME_REMOTE_OP_ERR			= 0x14,
	MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR	= 0x15,
	MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR		= 0x16,
	MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR		= 0x22,
};

enum {
	MLX4_CQE_STATUS_L4_CSUM		= 1 << 2,
	MLX4_CQE_STATUS_IPV4		= 1 << 6,
	MLX4_CQE_STATUS_IPV4F		= 1 << 7,
	MLX4_CQE_STATUS_IPV6		= 1 << 8,
	MLX4_CQE_STATUS_IPV4OPT		= 1 << 9,
	MLX4_CQE_STATUS_TCP		= 1 << 10,
	MLX4_CQE_STATUS_UDP		= 1 << 11,
	MLX4_CQE_STATUS_IPOK		= 1 << 12
};


struct mlx4_cqe {
	uint32_t	vlan_my_qpn;
	uint32_t	immed_rss_invalid;
	uint32_t	g_mlpath_rqpn;
	union {
		struct {
			union {
				struct {
					uint16_t  sl_vid;
					uint16_t  rlid;
				};
				uint32_t  timestamp_16_47;
			};
			uint16_t  status;
			uint8_t   reserved2;
			uint8_t   badfcs_enc;
		};
		struct {
			uint16_t reserved4;
			uint8_t  smac[6];
		};
	};
	uint32_t	byte_cnt;
	uint16_t	wqe_index;
	uint16_t	checksum;
	uint8_t		reserved5[1];
	uint16_t	timestamp_0_15;
	uint8_t		owner_sr_opcode;
} __attribute__((packed));

struct mlx4_err_cqe {
	uint32_t	vlan_my_qpn;
	uint32_t	reserved1[5];
	uint16_t	wqe_index;
	uint8_t		vendor_err;
	uint8_t		syndrome;
	uint8_t		reserved2[3];
	uint8_t		owner_sr_opcode;
};

static struct mlx4_cqe *get_cqe(struct mlx4_cq *cq, int entry)
{
	return cq->buf.buf + entry * cq->cqe_size;
}

static void *get_sw_cqe(struct mlx4_cq *cq, int n)
{
	struct mlx4_cqe *cqe = get_cqe(cq, n & cq->ibv_cq.cqe);
	struct mlx4_cqe *tcqe = cq->cqe_size == 64 ? cqe + 1 : cqe;

	return (!!(tcqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK) ^
		!!(n & (cq->ibv_cq.cqe + 1))) ? NULL : cqe;
}

static struct mlx4_cqe *next_cqe_sw(struct mlx4_cq *cq)
{
	return get_sw_cqe(cq, cq->cons_index);
}

static void mlx4_handle_error_cqe(struct mlx4_err_cqe *cqe, struct ibv_wc *wc)
{
	if (cqe->syndrome == MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR)
		printf(PFX "local QP operation err "
		       "(QPN %06x, WQE index %x, vendor syndrome %02x, "
		       "opcode = %02x)\n",
		       htonl(cqe->vlan_my_qpn), htonl(cqe->wqe_index),
		       cqe->vendor_err,
		       cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);

	switch (cqe->syndrome) {
	case MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IBV_WC_LOC_LEN_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IBV_WC_LOC_QP_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IBV_WC_LOC_PROT_ERR;
		break;
	case MLX4_CQE_SYNDROME_WR_FLUSH_ERR:
		wc->status = IBV_WC_WR_FLUSH_ERR;
		break;
	case MLX4_CQE_SYNDROME_MW_BIND_ERR:
		wc->status = IBV_WC_MW_BIND_ERR;
		break;
	case MLX4_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IBV_WC_BAD_RESP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IBV_WC_LOC_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IBV_WC_REM_INV_REQ_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IBV_WC_REM_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IBV_WC_REM_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IBV_WC_RETRY_EXC_ERR;
		break;
	case MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IBV_WC_RNR_RETRY_EXC_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		wc->status = IBV_WC_REM_ABORT_ERR;
		break;
	default:
		wc->status = IBV_WC_GENERAL_ERR;
		break;
	}

	wc->vendor_err = cqe->vendor_err;
}

static int mlx4_poll_one(struct mlx4_cq *cq,
			 struct mlx4_qp **cur_qp,
			 struct ibv_exp_wc *wc,
			 uint32_t wc_size, int is_exp)
{
	struct mlx4_wq *wq;
	struct mlx4_cqe *cqe;
	struct mlx4_srq *srq;
	uint32_t qpn;
	uint32_t g_mlpath_rqpn;
	uint16_t wqe_index;
	int is_error;
	int is_send;
	int size;
	int left;
	int list_len;
	int i;
	struct mlx4_inlr_rbuff *rbuffs;
	uint8_t *sbuff;
	int timestamp_en = !!(cq->creation_flags &
			      IBV_EXP_CQ_TIMESTAMP);
	uint64_t exp_wc_flags = 0;
	uint64_t wc_flags = 0;
	cqe = next_cqe_sw(cq);
	if (!cqe)
		return CQ_EMPTY;

	if (cq->cqe_size == 64)
		++cqe;

	++cq->cons_index;

	VALGRIND_MAKE_MEM_DEFINED(cqe, sizeof *cqe);

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	qpn = ntohl(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK;
	wc->qp_num = qpn;

	is_send  = cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK;

	/* include checksum as work around for calc opcode */
	is_error = (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
		MLX4_CQE_OPCODE_ERROR && (cqe->checksum & 0xff);

	if ((qpn & MLX4_XRC_QPN_BIT) && !is_send) {
		/*
		 * We do not have to take the XSRQ table lock here,
		 * because CQs will be locked while SRQs are removed
		 * from the table.
		 */
		*cur_qp = NULL;
		srq = mlx4_find_xsrq(&to_mctx(cq->ibv_cq.context)->xsrq_table,
				     ntohl(cqe->g_mlpath_rqpn) & MLX4_CQE_QPN_MASK);
		if (!srq)
			return CQ_POLL_ERR;
	} else {
		if (unlikely(!*cur_qp || (qpn != (*cur_qp)->verbs_qp.qp.qp_num))) {
			/*
			 * We do not have to take the QP table lock here,
			 * because CQs will be locked while QPs are removed
			 * from the table.
			 */
			*cur_qp = mlx4_find_qp(to_mctx(cq->ibv_cq.context), qpn);
			if (unlikely(!*cur_qp))
				return CQ_POLL_ERR;
		}
		if (is_exp) {
			wc->qp = &((*cur_qp)->verbs_qp.qp);
			exp_wc_flags |= IBV_EXP_WC_QP;
		}
		srq = ((*cur_qp)->verbs_qp.qp.srq) ? to_msrq((*cur_qp)->verbs_qp.qp.srq) : NULL;
	}

	if (is_send) {
		wq = &(*cur_qp)->sq;
		wqe_index = ntohs(cqe->wqe_index);
		wq->tail += (uint16_t) (wqe_index - (uint16_t) wq->tail);
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	} else if (srq) {
		wqe_index = htons(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_index];
		mlx4_free_srq_wqe(srq, wqe_index);
		if (is_exp) {
			wc->srq = &(srq->verbs_srq.srq);
			exp_wc_flags |= IBV_EXP_WC_SRQ;
		}
	} else {
		wq = &(*cur_qp)->rq;
		wqe_index = wq->tail & (wq->wqe_cnt - 1);
		wc->wr_id = wq->wrid[wqe_index];
		++wq->tail;
	}

	if (unlikely(is_error)) {
		mlx4_handle_error_cqe((struct mlx4_err_cqe *)cqe,
				      (struct ibv_wc *)wc);
		return CQ_OK;
	}

	wc->status = IBV_WC_SUCCESS;

	if (timestamp_en && offsetof(struct ibv_exp_wc, timestamp) < wc_size)  {
		/* currently, only CQ_CREATE_WITH_TIMESTAMPING_RAW is
		 * supported. CQ_CREATE_WITH_TIMESTAMPING_SYS isn't
		 * supported */
		if (cq->creation_flags &
		    IBV_EXP_CQ_TIMESTAMP_TO_SYS_TIME)
			wc->timestamp = 0;
		else {
			wc->timestamp =
				(uint64_t)(ntohl(cqe->timestamp_16_47) +
					   !cqe->timestamp_0_15) << 16
				| (uint64_t)ntohs(cqe->timestamp_0_15);
			exp_wc_flags |= IBV_EXP_WC_WITH_TIMESTAMP;
		}
	}

	if (is_send) {
		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_OPCODE_CALC_RDMA_WRITE_IMM:
		case MLX4_OPCODE_RDMA_WRITE_IMM:
			wc_flags |= IBV_WC_WITH_IMM;
		case MLX4_OPCODE_RDMA_WRITE:
			wc->exp_opcode    = IBV_EXP_WC_RDMA_WRITE;
			break;
		case MLX4_OPCODE_SEND_IMM:
			wc_flags |= IBV_WC_WITH_IMM;
		case MLX4_OPCODE_SEND:
			wc->exp_opcode    = IBV_EXP_WC_SEND;
			break;
		case MLX4_OPCODE_RDMA_READ:
			wc->exp_opcode    = IBV_EXP_WC_RDMA_READ;
			wc->byte_len  = ntohl(cqe->byte_cnt);
			break;
		case MLX4_OPCODE_ATOMIC_CS:
			wc->exp_opcode    = IBV_EXP_WC_COMP_SWAP;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_ATOMIC_FA:
			wc->exp_opcode    = IBV_EXP_WC_FETCH_ADD;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_ATOMIC_MASK_CS:
			wc->exp_opcode    = IBV_EXP_WC_MASKED_COMP_SWAP;
			break;
		case MLX4_OPCODE_ATOMIC_MASK_FA:
			wc->exp_opcode    = IBV_EXP_WC_MASKED_FETCH_ADD;
			break;
		case MLX4_OPCODE_LOCAL_INVAL:
			((struct ibv_wc *)wc)->opcode    = IBV_WC_LOCAL_INV;
			break;
		case MLX4_OPCODE_BIND_MW:
			wc->exp_opcode    = IBV_EXP_WC_BIND_MW;
			break;
		case MLX4_OPCODE_SEND_INVAL:
			((struct ibv_wc *)wc)->opcode    = IBV_WC_SEND;
			break;
		default:
			/* assume it's a send completion */
			wc->exp_opcode    = IBV_EXP_WC_SEND;
			break;
		}
	} else {
		wc->byte_len = ntohl(cqe->byte_cnt);
		if ((*cur_qp) && (*cur_qp)->max_inlr_sg &&
		    (cqe->owner_sr_opcode & MLX4_CQE_INL_SCATTER_MASK)) {
			rbuffs = (*cur_qp)->inlr_buff.buff[wqe_index].sg_list;
			list_len = (*cur_qp)->inlr_buff.buff[wqe_index].list_len;
			sbuff = mlx4_get_recv_wqe((*cur_qp), wqe_index);
			left = wc->byte_len;
			for (i = 0; (i < list_len) && left; i++) {
				size = min(rbuffs->rlen, left);
				memcpy(rbuffs->rbuff, sbuff, size);
				left -= size;
				rbuffs++;
				sbuff += size;
			}
			if (left) {
				wc->status = IBV_WC_LOC_LEN_ERR;
				return CQ_OK;
			}
		}

		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
			wc->exp_opcode   = IBV_EXP_WC_RECV_RDMA_WITH_IMM;
			wc_flags = IBV_WC_WITH_IMM;
			wc->imm_data = cqe->immed_rss_invalid;
			break;
		case MLX4_RECV_OPCODE_SEND_INVAL:
			((struct ibv_wc *)wc)->opcode   = IBV_WC_RECV;
			((struct ibv_wc *)wc)->wc_flags |= IBV_WC_WITH_INV;
			wc->imm_data = ntohl(cqe->immed_rss_invalid);
			break;
		case MLX4_RECV_OPCODE_SEND:
			wc->exp_opcode   = IBV_EXP_WC_RECV;
			wc_flags = 0;
			break;
		case MLX4_RECV_OPCODE_SEND_IMM:
			wc->exp_opcode   = IBV_EXP_WC_RECV;
			wc_flags = IBV_WC_WITH_IMM;
			wc->imm_data = cqe->immed_rss_invalid;
			break;
		}

		if (!timestamp_en) {
			exp_wc_flags |= IBV_EXP_WC_WITH_SLID;
			wc->slid = ntohs(cqe->rlid);
		}
		g_mlpath_rqpn	   = ntohl(cqe->g_mlpath_rqpn);
		wc->src_qp	   = g_mlpath_rqpn & 0xffffff;
		wc->dlid_path_bits = (g_mlpath_rqpn >> 24) & 0x7f;
		wc_flags	  |= g_mlpath_rqpn & 0x80000000 ? IBV_WC_GRH : 0;
		wc->pkey_index     = ntohl(cqe->immed_rss_invalid) & 0x7f;
		/* When working with xrc srqs, don't have qp to check link layer.
		  * Using IB SL, should consider Roce. (TBD)
		*/
		/* sl is invalid when timestamp is used */
		if (!timestamp_en) {
			if ((*cur_qp) && (*cur_qp)->link_layer ==
			    IBV_LINK_LAYER_ETHERNET)
				wc->sl = ntohs(cqe->sl_vid) >> 13;
			else
				wc->sl = ntohs(cqe->sl_vid) >> 12;
			exp_wc_flags |= IBV_EXP_WC_WITH_SL;
		}
		if (is_exp && *cur_qp) {
			if ((*cur_qp)->qp_cap_cache & MLX4_RX_CSUM_MODE_IP_OK_IP_NON_TCP_UDP)
			/* Only ConnectX-3 Pro reports checksum for now) */
				exp_wc_flags |=
				MLX4_TRANSPOSE(cqe->badfcs_enc,
					MLX4_CQE_STATUS_L4_CSUM,
					(uint64_t)IBV_EXP_WC_RX_TCP_UDP_CSUM_OK) |
				mlx4_transpose_uint16_t(cqe->status,
					htons(MLX4_CQE_STATUS_IPOK),
					(uint64_t)IBV_EXP_WC_RX_IP_CSUM_OK) |
				mlx4_transpose_uint16_t(cqe->status,
					htons(MLX4_CQE_STATUS_IPV4),
					(uint64_t)IBV_EXP_WC_RX_IPV4_PACKET) |
				mlx4_transpose_uint16_t(cqe->status,
					htons(MLX4_CQE_STATUS_IPV6),
					(uint64_t)IBV_EXP_WC_RX_IPV6_PACKET);
			if ((*cur_qp)->qp_cap_cache & MLX4_RX_VXLAN) {
				exp_wc_flags |=
				mlx4_transpose_uint32_t(cqe->vlan_my_qpn,
				htonl(MLX4_CQE_L2_TUNNEL),
				(uint64_t)IBV_EXP_WC_RX_TUNNEL_PACKET) |
				mlx4_transpose_uint32_t(cqe->vlan_my_qpn,
					htonl(MLX4_CQE_L2_TUNNEL_IPOK),
					(uint64_t)IBV_EXP_WC_RX_OUTER_IP_CSUM_OK) |
				mlx4_transpose_uint32_t(cqe->vlan_my_qpn,
					htonl(MLX4_CQE_L2_TUNNEL_L4_CSUM),
					(uint64_t)IBV_EXP_WC_RX_OUTER_TCP_UDP_CSUM_OK) |
				mlx4_transpose_uint32_t(cqe->vlan_my_qpn,
					htonl(MLX4_CQE_L2_TUNNEL_IPV4),
					(uint64_t)IBV_EXP_WC_RX_OUTER_IPV4_PACKET);
				exp_wc_flags |=
				MLX4_TRANSPOSE(~exp_wc_flags,
						IBV_EXP_WC_RX_OUTER_IPV4_PACKET,
						IBV_EXP_WC_RX_OUTER_IPV6_PACKET);
			}
		}
	}

	if (is_exp)
		wc->exp_wc_flags = exp_wc_flags | (uint64_t)wc_flags;

	((struct ibv_wc *)wc)->wc_flags = wc_flags;

	return CQ_OK;
}

#if defined(__x86_64__) || defined(__i386__)
static inline unsigned long get_cycles()
{
	unsigned low, high;
	unsigned long long val;
	asm volatile ("rdtsc" : "=a" (low), "=d" (high));
	val = high;
	val = (val << 32) | low;
	return val;
}
#else
static inline unsigned long get_cycles()
{
	return 0;
}
#endif

static void mlx4_stall_poll_cq()
{
	int i;

	for (i = 0; i < mlx4_stall_num_loop; i++)
		(void)get_cycles();
}

int mlx4_poll_cq(struct ibv_cq *ibcq, int ne, struct ibv_exp_wc *wc,
		 uint32_t wc_size, int is_exp)
{
	struct mlx4_cq *cq = to_mcq(ibcq);
	struct mlx4_qp *qp = NULL;
	int npolled;
	int err = CQ_OK;

	if (unlikely(cq->stall_next_poll)) {
		cq->stall_next_poll = 0;
		mlx4_stall_poll_cq();
	}
	mlx4_lock(&cq->lock);
	
	for (npolled = 0; npolled < ne; ++npolled) {
		err = mlx4_poll_one(cq, &qp, ((void *)wc) + npolled * wc_size,
				    wc_size, is_exp);
		if (unlikely(err != CQ_OK))
			break;
	}

	if (likely(npolled || err == CQ_POLL_ERR))
		mlx4_update_cons_index(cq);

	mlx4_unlock(&cq->lock);

	if (unlikely(cq->stall_enable && err == CQ_EMPTY))
		cq->stall_next_poll = 1;
	
	return err == CQ_POLL_ERR ? err : npolled;
}

int mlx4_exp_poll_cq(struct ibv_cq *ibcq, int num_entries,
		     struct ibv_exp_wc *wc, uint32_t wc_size)
{
	return mlx4_poll_cq(ibcq, num_entries, wc, wc_size, 1);
}

int mlx4_poll_ibv_cq(struct ibv_cq *ibcq, int ne, struct ibv_wc *wc)
{
	return mlx4_poll_cq(ibcq, ne, (struct ibv_exp_wc *)wc, sizeof(*wc), 0);
}

int mlx4_arm_cq(struct ibv_cq *ibvcq, int solicited)
{
	struct mlx4_cq *cq = to_mcq(ibvcq);
	uint32_t doorbell[2];
	uint32_t sn;
	uint32_t ci;
	uint32_t cmd;

	sn  = cq->arm_sn & 3;
	ci  = cq->cons_index & 0xffffff;
	cmd = solicited ? MLX4_CQ_DB_REQ_NOT_SOL : MLX4_CQ_DB_REQ_NOT;

	*cq->arm_db = htonl(sn << 28 | cmd | ci);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	doorbell[0] = htonl(sn << 28 | cmd | cq->cqn);
	doorbell[1] = htonl(ci);

	mlx4_write64(doorbell, to_mctx(ibvcq->context), MLX4_CQ_DOORBELL);

	return 0;
}

void mlx4_cq_event(struct ibv_cq *cq)
{
	to_mcq(cq)->arm_sn++;
}

void __mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn, struct mlx4_srq *srq)
{
	struct mlx4_cqe *cqe, *dest;
	uint32_t prod_index;
	uint8_t owner_bit;
	int nfreed = 0;
	int cqe_inc = cq->cqe_size == 64 ? 1 : 0;

	if (cq->last_qp && cq->last_qp->verbs_qp.qp.qp_num == qpn)
		cq->last_qp = NULL;
	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->cons_index; get_sw_cqe(cq, prod_index); ++prod_index)
		if (prod_index == cq->cons_index + cq->ibv_cq.cqe)
			break;

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibv_cq.cqe);
		cqe += cqe_inc;
		if (srq && srq->ext_srq &&
		    ntohl(cqe->g_mlpath_rqpn & MLX4_CQE_QPN_MASK) == srq->verbs_srq.srq_num &&
		    !(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK)) {
			mlx4_free_srq_wqe(srq, ntohs(cqe->wqe_index));
			++nfreed;
		} else if ((ntohl(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK) == qpn) {
			if (srq && !(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))
				mlx4_free_srq_wqe(srq, ntohs(cqe->wqe_index));
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibv_cq.cqe);
			dest += cqe_inc;
			owner_bit = dest->owner_sr_opcode & MLX4_CQE_OWNER_MASK;
			memcpy(dest, cqe, sizeof *cqe);
			dest->owner_sr_opcode = owner_bit |
				(dest->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);
		}
	}

	if (nfreed) {
		cq->cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		wmb();
		mlx4_update_cons_index(cq);
	}
}

void mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn, struct mlx4_srq *srq)
{
	mlx4_lock(&cq->lock);
	__mlx4_cq_clean(cq, qpn, srq);
	mlx4_unlock(&cq->lock);
}

int mlx4_get_outstanding_cqes(struct mlx4_cq *cq)
{
	uint32_t i;

	for (i = cq->cons_index; get_sw_cqe(cq, i); ++i)
		;

	return i - cq->cons_index;
}

void mlx4_cq_resize_copy_cqes(struct mlx4_cq *cq, void *buf, int old_cqe)
{
	struct mlx4_cqe *cqe;
	int i;
	int cqe_inc = cq->cqe_size == 64 ? 1 : 0;

	i = cq->cons_index;
	cqe = get_cqe(cq, (i & old_cqe));
	cqe += cqe_inc;

	while ((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) != MLX4_CQE_OPCODE_RESIZE) {
		cqe->owner_sr_opcode = (cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK) |
			(((i + 1) & (cq->ibv_cq.cqe + 1)) ? MLX4_CQE_OWNER_MASK : 0);
		memcpy(buf + ((i + 1) & cq->ibv_cq.cqe) * cq->cqe_size,
		       cqe - cqe_inc, cq->cqe_size);
		++i;
		cqe = get_cqe(cq, (i & old_cqe));
		cqe += cqe_inc;
	}

	++cq->cons_index;
}

int mlx4_alloc_cq_buf(struct mlx4_context *mctx, struct mlx4_buf *buf, int nent,
			int entry_size)
{
	struct mlx4_device *dev = to_mdev(mctx->ibv_ctx.device);
	int ret;
	enum mlx4_alloc_type alloc_type;
	enum mlx4_alloc_type default_alloc_type = MLX4_ALLOC_TYPE_PREFER_CONTIG;

	if (mlx4_use_huge(&mctx->ibv_ctx, "HUGE_CQ"))
		default_alloc_type = MLX4_ALLOC_TYPE_HUGE;

	mlx4_get_alloc_type(&mctx->ibv_ctx, MLX4_CQ_PREFIX, &alloc_type,
			    default_alloc_type);

	ret = mlx4_alloc_prefered_buf(mctx, buf,
			align(nent * entry_size, dev->page_size),
			dev->page_size,
			alloc_type,
			MLX4_CQ_PREFIX);

	if (ret)
		return -1;

	memset(buf->buf, 0, nent * entry_size);

	return 0;
}

/*
 *  poll  family functions
 */
static inline int drain_rx(struct mlx4_cq *cq, struct mlx4_cqe *cqe,
			   struct mlx4_qp *cur_qp, uint8_t *buf, uint32_t *inl) __attribute__((always_inline));
static inline int drain_rx(struct mlx4_cq *cq, struct mlx4_cqe *cqe,
			   struct mlx4_qp *cur_qp, uint8_t *buf, uint32_t *inl)
{
	struct mlx4_srq *srq;
	uint32_t qpn;
	uint16_t wqe_index;

	qpn = ntohl(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK;


	if (unlikely(!cur_qp || (qpn != cur_qp->verbs_qp.qp.qp_num))) {
		if (unlikely(qpn & MLX4_XRC_QPN_BIT)) {
			/*
			 * We do not have to take the XSRQ table lock here,
			 * because CQs will be locked while SRQs are removed
			 * from the table.
			 */
			cur_qp = NULL;
			srq = mlx4_find_xsrq(&to_mctx(cq->ibv_cq.context)->xsrq_table,
					     ntohl(cqe->g_mlpath_rqpn) & MLX4_CQE_QPN_MASK);
			if (!srq)
				return CQ_POLL_ERR;

			/* Advance indexes only on success */
			wqe_index = htons(cqe->wqe_index);
			mlx4_free_srq_wqe(to_msrq(cur_qp->verbs_qp.qp.srq), wqe_index);

			++cq->cons_index;

			return CQ_OK;
		}

		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		cur_qp = mlx4_find_qp(to_mctx(cq->ibv_cq.context), qpn);
		if (unlikely(!cur_qp))
			return CQ_POLL_ERR;
		cq->last_qp = cur_qp;
	}

	if (!cur_qp->max_inlr_sg) {
		/* Advance indexes only on success to enable getting
		 * the full CQE with ibv_poll_cq in case of failure
		 */
		if (unlikely(cur_qp->verbs_qp.qp.srq)) {
			wqe_index = htons(cqe->wqe_index);
			mlx4_free_srq_wqe(to_msrq(cur_qp->verbs_qp.qp.srq), wqe_index);
		} else {
			++cur_qp->rq.tail;
		}
		++cq->cons_index;

		return CQ_OK;
	}

	/* We get here only when cur_qp->max_inlr_sg != 0 */
	if (likely(cqe->owner_sr_opcode & MLX4_CQE_INL_SCATTER_MASK)) {
		int size;
		int left;
		int list_len;
		int i;
		struct mlx4_inlr_rbuff *rbuffs;
		uint8_t *sbuff;
		int is_error;

		/* include checksum as work around for calc opcode */
		is_error = (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
			   MLX4_CQE_OPCODE_ERROR && (cqe->checksum & 0xff);
		if (unlikely(is_error))
			return CQ_POLL_ERR;

		wqe_index = cur_qp->rq.tail & (cur_qp->rq.wqe_cnt - 1);
		sbuff = mlx4_get_recv_wqe(cur_qp, wqe_index);
		left = ntohl(cqe->byte_cnt);
		if (likely(buf)) {
			*inl = 1;
			memcpy(buf, sbuff, left);
		} else {
			rbuffs = cur_qp->inlr_buff.buff[wqe_index].sg_list;
			list_len = cur_qp->inlr_buff.buff[wqe_index].list_len;
			for (i = 0; (i < list_len) && left; i++) {
				size = min(rbuffs->rlen, left);
				memcpy(rbuffs->rbuff, sbuff, size);
				left -= size;
				rbuffs++;
				sbuff += size;
			}
			if (left)
				return CQ_POLL_ERR;
		}
	}

	/* Advance indexes only on success to enable getting
	 * the full CQE with ibv_poll_cq in case of failure
	 */
	++cur_qp->rq.tail;

	++cq->cons_index;

	return CQ_OK;
}

static inline int update_sq_tail(struct mlx4_cq *cq, struct mlx4_cqe *cqe,
				 struct mlx4_qp *cur_qp) __attribute__((always_inline));
static inline int update_sq_tail(struct mlx4_cq *cq, struct mlx4_cqe *cqe,
				 struct mlx4_qp *cur_qp)
{
	uint32_t qpn;

	qpn = ntohl(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK;
	if (unlikely(!cur_qp || (qpn != cur_qp->verbs_qp.qp.qp_num))) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		cur_qp = mlx4_find_qp(to_mctx(cq->ibv_cq.context), qpn);
		if (unlikely(!cur_qp))
			return CQ_POLL_ERR;
		cq->last_qp = cur_qp;
	}

	/* Advance indexes only on success */
	cur_qp->sq.tail +=  (uint16_t)(ntohs(cqe->wqe_index) - (uint16_t)cur_qp->sq.tail);
	++cq->cons_index;

	return CQ_OK;
}

static inline struct mlx4_cqe *get_next_cqe(struct mlx4_cq *cq, int const cqe_size) __attribute__((always_inline));
static inline struct mlx4_cqe *get_next_cqe(struct mlx4_cq *cq, int const cqe_size)
{
	int cqe_off = (cqe_size & 64) >> 1; /* CQE offset is 32 bytes in case cqe_size == 64 */
	struct mlx4_cqe *cqe = cq->buf.buf + (cq->cons_index & cq->ibv_cq.cqe) * cqe_size + cqe_off;

	if (!!(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK) ^
	    !!(cq->cons_index & (cq->ibv_cq.cqe + 1)))
		return NULL;

	VALGRIND_MAKE_MEM_DEFINED(cqe, sizeof *cqe);

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	return cqe;
}

static inline int32_t poll_cnt(struct ibv_cq *ibcq, uint32_t max_entries, const int use_lock, const int cqe_size) __attribute__((always_inline));
static inline int32_t poll_cnt(struct ibv_cq *ibcq, uint32_t max_entries, const int use_lock, const int cqe_size)
{
	struct mlx4_cq *cq = to_mcq(ibcq);
	struct mlx4_cqe *cqe;
	int npolled;
	int err = CQ_OK;

	if (unlikely(use_lock))
		mlx4_lock(&cq->lock);

	for (npolled = 0; npolled < max_entries; ++npolled) {
		cqe = get_next_cqe(cq, cqe_size);
		if (!cqe) {
			err = CQ_EMPTY;
			break;
		}
		/*
		 * Make sure we read CQ entry contents after we've checked the
		 * ownership bit.
		 */
		rmb();

		if (likely(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))
			err = update_sq_tail(cq, cqe, cq->last_qp);
		else
			err = drain_rx(cq, cqe, cq->last_qp, NULL, NULL);

		if (unlikely(err != CQ_OK))
			break;
	}

	if (likely(npolled)) {
		mlx4_update_cons_index(cq);
		err = CQ_OK;
	}

	if (unlikely(use_lock))
		mlx4_unlock(&cq->lock);

	return err == CQ_POLL_ERR ? -1 : npolled;
}

static inline int32_t get_flags(struct mlx4_qp *cur_qp, struct mlx4_cqe *cqe) __attribute__((always_inline));
static inline int32_t get_flags(struct mlx4_qp *cur_qp, struct mlx4_cqe *cqe)
{
	int32_t flags;
	int32_t tmp = 0;

	/* Only ConnectX-3 Pro reports checksum for now */
	if (likely(cur_qp)) {
		/*
		 * The relevant bits are in different locations on their
		 * CQE fields therefore we can join them in one 32bit
		 * variable.
		 */
		if (cur_qp->qp_cap_cache & MLX4_RX_CSUM_MODE_IP_OK_IP_NON_TCP_UDP)
			tmp = (cqe->badfcs_enc & MLX4_CQE_STATUS_L4_CSUM) |
			      (ntohs(cqe->status) & (MLX4_CQE_STATUS_IPOK |
						MLX4_CQE_STATUS_IPV4 |
						MLX4_CQE_STATUS_IPV6));

		if (cur_qp->qp_cap_cache & MLX4_RX_VXLAN)
			tmp |= ntohl(cqe->vlan_my_qpn) & (MLX4_CQE_L2_TUNNEL |
							MLX4_CQE_L2_TUNNEL_IPOK |
							MLX4_CQE_L2_TUNNEL_L4_CSUM |
							MLX4_CQE_L2_TUNNEL_IPV4);

		if (likely(tmp == cur_qp->cached_rx_csum_flags)) {
			flags = cur_qp->transposed_rx_csum_flags;
		} else {
			flags = mlx4_transpose(tmp, MLX4_CQE_STATUS_IPOK,	IBV_EXP_CQ_RX_IP_CSUM_OK)	|
				mlx4_transpose(tmp, MLX4_CQE_STATUS_L4_CSUM,	IBV_EXP_CQ_RX_TCP_UDP_CSUM_OK)	|
				mlx4_transpose(tmp, MLX4_CQE_STATUS_IPV4,	IBV_EXP_CQ_RX_IPV4_PACKET)	|
				mlx4_transpose(tmp, MLX4_CQE_STATUS_IPV6,	IBV_EXP_CQ_RX_IPV6_PACKET)	|
				mlx4_transpose(tmp, MLX4_CQE_L2_TUNNEL,		IBV_EXP_CQ_RX_TUNNEL_PACKET)	|
				mlx4_transpose(tmp, MLX4_CQE_L2_TUNNEL_IPOK,	IBV_EXP_CQ_RX_OUTER_IP_CSUM_OK)	|
				mlx4_transpose(tmp, MLX4_CQE_L2_TUNNEL_L4_CSUM,	IBV_EXP_CQ_RX_OUTER_TCP_UDP_CSUM_OK)	|
				mlx4_transpose(tmp, MLX4_CQE_L2_TUNNEL_IPV4,	IBV_EXP_CQ_RX_OUTER_IPV4_PACKET)	|
				mlx4_transpose(~tmp, MLX4_CQE_L2_TUNNEL_IPV4,	IBV_EXP_CQ_RX_OUTER_IPV6_PACKET);
			cur_qp->cached_rx_csum_flags = tmp;
			cur_qp->transposed_rx_csum_flags = flags;
		}

		return flags;
	}

	return 0;
}

static inline int32_t poll_length(struct ibv_cq *ibcq, void *buf, uint32_t *inl,
				  const int use_lock, const int cqe_size,
				  uint32_t *flags) __attribute__((always_inline));
static inline int32_t poll_length(struct ibv_cq *ibcq, void *buf, uint32_t *inl,
				  const int use_lock, const int cqe_size,
				  uint32_t *flags)
{
	struct mlx4_cq *cq = to_mcq(ibcq);
	struct mlx4_cqe *cqe;
	int32_t size = 0;
	int err;

	if (unlikely(use_lock))
		mlx4_lock(&cq->lock);

	cqe = get_next_cqe(cq, cqe_size);
	if (cqe) {
		/*
		 * Make sure we read CQ entry contents after we've checked the
		 * ownership bit.
		 */
		rmb();
		if (likely(!(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))) {
			err = drain_rx(cq, cqe, cq->last_qp, buf, inl);
			if (likely(err == CQ_OK)) {
				size = ntohl(cqe->byte_cnt);
				if (flags)
					*flags = get_flags(cq->last_qp, cqe);
				mlx4_update_cons_index(cq);
			}
		} else {
			err = CQ_POLL_ERR;
		}

	} else {
		err = CQ_EMPTY;
	}


	if (unlikely(use_lock))
		mlx4_unlock(&cq->lock);

	return err == CQ_POLL_ERR ? -1 : size;
}

int32_t mlx4_poll_cnt_safe(struct ibv_cq *ibcq, uint32_t max) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_cnt_safe(struct ibv_cq *ibcq, uint32_t max)
{
	struct mlx4_cq *cq = to_mcq(ibcq);

	return poll_cnt(ibcq, max, 1, cq->cqe_size);
}

int32_t mlx4_poll_cnt_unsafe_other(struct ibv_cq *ibcq, uint32_t max) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_cnt_unsafe_other(struct ibv_cq *ibcq, uint32_t max)
{
	struct mlx4_cq *cq = to_mcq(ibcq);

	return poll_cnt(ibcq, max, 0, cq->cqe_size);
}

int32_t mlx4_poll_cnt_unsafe_cqe32(struct ibv_cq *ibcq, uint32_t max) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_cnt_unsafe_cqe32(struct ibv_cq *ibcq, uint32_t max)
{
	return poll_cnt(ibcq, max, 0, 32);
}

int32_t mlx4_poll_cnt_unsafe_cqe64(struct ibv_cq *ibcq, uint32_t max) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_cnt_unsafe_cqe64(struct ibv_cq *ibcq, uint32_t max)
{
	return poll_cnt(ibcq, max, 0, 64);
}

int32_t mlx4_poll_cnt_unsafe_cqe128(struct ibv_cq *ibcq, uint32_t max) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_cnt_unsafe_cqe128(struct ibv_cq *ibcq, uint32_t max)
{
	return poll_cnt(ibcq, max, 0, 128);
}

int32_t mlx4_poll_length_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl)
{
	struct mlx4_cq *cq = to_mcq(ibcq);

	return poll_length(ibcq, buf, inl, 1, cq->cqe_size, NULL);
}

int32_t mlx4_poll_length_unsafe_other(struct ibv_cq *ibcq, void *buf, uint32_t *inl) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_unsafe_other(struct ibv_cq *ibcq, void *buf, uint32_t *inl)
{
	struct mlx4_cq *cq = to_mcq(ibcq);

	return poll_length(ibcq, buf, inl, 0, cq->cqe_size, NULL);
}

int32_t mlx4_poll_length_unsafe_cqe32(struct ibv_cq *cq, void *buf, uint32_t *inl) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_unsafe_cqe32(struct ibv_cq *cq, void *buf, uint32_t *inl)
{
	return poll_length(cq, buf, inl, 0, 32, NULL);
}

int32_t mlx4_poll_length_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl)
{
	return poll_length(cq, buf, inl, 0, 64, NULL);
}

int32_t mlx4_poll_length_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl)
{
	return poll_length(cq, buf, inl, 0, 128, NULL);
}

int32_t mlx4_poll_length_flags_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_flags_safe(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags)
{
	struct mlx4_cq *cq = to_mcq(ibcq);

	return poll_length(ibcq, buf, inl, 1, cq->cqe_size, flags);
}

int32_t mlx4_poll_length_flags_unsafe_other(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_flags_unsafe_other(struct ibv_cq *ibcq, void *buf, uint32_t *inl, uint32_t *flags)
{
	struct mlx4_cq *cq = to_mcq(ibcq);

	return poll_length(ibcq, buf, inl, 0, cq->cqe_size, flags);
}

int32_t mlx4_poll_length_flags_unsafe_cqe32(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_flags_unsafe_cqe32(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags)
{
	return poll_length(cq, buf, inl, 0, 32, flags);
}

int32_t mlx4_poll_length_flags_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_flags_unsafe_cqe64(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags)
{
	return poll_length(cq, buf, inl, 0, 64, flags);
}

int32_t mlx4_poll_length_flags_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags) __MLX4_ALGN_FUNC__;
int32_t mlx4_poll_length_flags_unsafe_cqe128(struct ibv_cq *cq, void *buf, uint32_t *inl, uint32_t *flags)
{
	return poll_length(cq, buf, inl, 0, 128, flags);
}

static struct ibv_exp_cq_family mlx4_poll_cq_family_safe = {
	.poll_cnt = mlx4_poll_cnt_safe,
	.poll_length = mlx4_poll_length_safe,
	.poll_length_flags = mlx4_poll_length_flags_safe
};

enum mlx4_poll_cq_cqe_sizes {
	MLX4_POLL_CQ_CQE_32		= 0,
	MLX4_POLL_CQ_CQE_64		= 1,
	MLX4_POLL_CQ_CQE_128		= 2,
	MLX4_POLL_CQ_CQE_OTHER		= 3,
	MLX4_POLL_CQ_NUM_CQE_SIZES	= 4,
};

static struct ibv_exp_cq_family mlx4_poll_cq_family_unsafe_tbl[MLX4_POLL_CQ_NUM_CQE_SIZES] = {
		[MLX4_POLL_CQ_CQE_32] = {
				.poll_cnt = mlx4_poll_cnt_unsafe_cqe32,
				.poll_length = mlx4_poll_length_unsafe_cqe32,
				.poll_length_flags = mlx4_poll_length_flags_unsafe_cqe32
		},
		[MLX4_POLL_CQ_CQE_64] = {
				.poll_cnt = mlx4_poll_cnt_unsafe_cqe64,
				.poll_length = mlx4_poll_length_unsafe_cqe64,
				.poll_length_flags = mlx4_poll_length_flags_unsafe_cqe64
		},
		[MLX4_POLL_CQ_CQE_128] = {
				.poll_cnt = mlx4_poll_cnt_unsafe_cqe128,
				.poll_length = mlx4_poll_length_unsafe_cqe128,
				.poll_length_flags = mlx4_poll_length_flags_unsafe_cqe128
		},
		[MLX4_POLL_CQ_CQE_OTHER] = {
				.poll_cnt = mlx4_poll_cnt_unsafe_other,
				.poll_length = mlx4_poll_length_unsafe_other,
				.poll_length_flags = mlx4_poll_length_flags_unsafe_other
		},
};

struct ibv_exp_cq_family *mlx4_get_poll_cq_family(struct mlx4_cq *cq,
						  struct ibv_exp_query_intf_params *params,
						  enum ibv_exp_query_intf_status *status)
{
	enum mlx4_poll_cq_cqe_sizes cqe_size = MLX4_POLL_CQ_CQE_OTHER;

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

	if (cq->model_flags & MLX4_CQ_MODEL_FLAG_THREAD_SAFE)
		return &mlx4_poll_cq_family_safe;

	if (cq->cqe_size == 32)
		cqe_size = MLX4_POLL_CQ_CQE_32;
	else if (cq->cqe_size == 64)
		cqe_size = MLX4_POLL_CQ_CQE_64;
	else if (cq->cqe_size == 128)
		cqe_size = MLX4_POLL_CQ_CQE_128;

	return &mlx4_poll_cq_family_unsafe_tbl[cqe_size];
}
