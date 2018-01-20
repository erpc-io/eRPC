/*
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
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/* Added for reg_mr mmap munmap system calls */
#include <sys/mman.h>
#include "mlx4.h"
#include "mlx4-abi.h"
#include "mlx4_exp.h"
#include "wqe.h"

static const char *qptype2key(enum ibv_qp_type type)
{
	switch (type) {
	case IBV_QPT_RC: return "HUGE_RC";
	case IBV_QPT_UC: return "HUGE_UC";
	case IBV_QPT_UD: return "HUGE_UD";
#ifdef _NOT_EXISTS_IN_OFED_2_0
	case IBV_QPT_RAW_PACKET: return "HUGE_RAW_ETH";
#endif

	default: return "HUGE_NA";
	}
}

static void update_qp_cap_cache(struct ibv_qp *qp)
{
	struct mlx4_context *ctx = to_mctx(qp->context);
	struct mlx4_qp *mqp = to_mqp(qp);

	if ((qp->qp_type == IBV_QPT_RAW_ETH) && (mqp->link_layer == IBV_LINK_LAYER_ETHERNET)) {
		if (ctx->exp_device_cap_flags & IBV_EXP_DEVICE_RX_CSUM_IP_PKT)
			mqp->qp_cap_cache |= MLX4_RX_CSUM_MODE_IP_OK_IP_NON_TCP_UDP;
		if (ctx->exp_device_cap_flags & IBV_EXP_DEVICE_VXLAN_SUPPORT)
			mqp->qp_cap_cache |= MLX4_RX_VXLAN;
	}
}

int update_port_data(struct ibv_qp *qp, uint8_t port_num)
{
	struct mlx4_qp *mqp = to_mqp(qp);
	struct ibv_port_attr port_attr;
	int err;

	err = ibv_query_port(qp->context, port_num, &port_attr);
	if (err)
		return err;

	mqp->link_layer = port_attr.link_layer;
	update_qp_cap_cache(qp);

	return 0;
}

int mlx4_exp_modify_qp(struct ibv_qp *qp, struct ibv_exp_qp_attr *attr,
		       uint64_t attr_mask)
{
	struct ibv_exp_modify_qp cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	if (attr_mask & IBV_QP_PORT) {
		ret = update_port_data(qp, attr->port_num);
		if (ret)
			return ret;
	}

	if (qp->state == IBV_QPS_RESET &&
	    (attr_mask & IBV_EXP_QP_STATE) &&
	    attr->qp_state == IBV_QPS_INIT) {
		mlx4_qp_init_sq_ownership(to_mqp(qp));
	}


	ret = ibv_exp_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof(cmd));

	if (!ret		       &&
	    (attr_mask & IBV_EXP_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RESET) {
		if (qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
				      qp->srq ? to_msrq(qp->srq) : NULL);
		if (qp->send_cq && qp->send_cq != qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

		mlx4_init_qp_indices(to_mqp(qp));
		if (to_mqp(qp)->rq.wqe_cnt)
			*to_mqp(qp)->db = 0;
	}

	return ret;
}

static int verify_sizes(struct ibv_exp_qp_init_attr *attr, struct mlx4_context *context)
{
	int size;
	int nsegs;

	if (attr->cap.max_send_wr     > context->max_qp_wr ||
	    attr->cap.max_recv_wr     > context->max_qp_wr ||
	    attr->cap.max_send_sge    > context->max_sge   ||
	    attr->cap.max_recv_sge    > context->max_sge)
		return -1;

	if (attr->cap.max_inline_data) {
		nsegs = num_inline_segs(attr->cap.max_inline_data, attr->qp_type);
		size = MLX4_MAX_WQE_SIZE - nsegs * sizeof(struct mlx4_wqe_inline_seg);
		switch (attr->qp_type) {
		case IBV_QPT_UD:
			size -= (sizeof(struct mlx4_wqe_ctrl_seg) +
				 sizeof(struct mlx4_wqe_datagram_seg));
			break;

		case IBV_QPT_RC:
		case IBV_QPT_UC:
			size -= (sizeof(struct mlx4_wqe_ctrl_seg) +
				 sizeof(struct mlx4_wqe_raddr_seg));
			break;

		default:
			return 0;
		}

		if (attr->cap.max_inline_data > size)
			return -1;
	}

	return 0;
}

static int mlx4_exp_alloc_qp_buf(struct ibv_context *context,
				 struct ibv_exp_qp_init_attr *attr,
				 struct mlx4_qp *qp)
{
	int ret;
	enum mlx4_alloc_type alloc_type;
	enum mlx4_alloc_type default_alloc_type = MLX4_ALLOC_TYPE_PREFER_CONTIG;
	const char *qp_huge_key;
	int i, wqe_size;

	qp->rq.max_gs = attr->cap.max_recv_sge;
	wqe_size = qp->rq.max_gs * sizeof(struct mlx4_wqe_data_seg);
	if ((attr->comp_mask & IBV_EXP_QP_INIT_ATTR_INL_RECV) && (attr->max_inl_recv)) {
		qp->max_inlr_sg = qp->rq.max_gs;
		wqe_size = max(wqe_size, attr->max_inl_recv);
	}
	for (qp->rq.wqe_shift = 4; 1 << qp->rq.wqe_shift < wqe_size; qp->rq.wqe_shift++)
		; /* nothing */

	if (qp->max_inlr_sg) {
		attr->max_inl_recv = 1 << qp->rq.wqe_shift;
		qp->max_inlr_sg = attr->max_inl_recv / sizeof(struct mlx4_wqe_data_seg);
	}

	if (qp->sq.wqe_cnt) {
		qp->sq.wrid = malloc(qp->sq.wqe_cnt * sizeof(uint64_t));
		if (!qp->sq.wrid)
			return -1;
	}

	if (qp->rq.wqe_cnt) {
		qp->rq.wrid = malloc(qp->rq.wqe_cnt * sizeof(uint64_t));
		if (!qp->rq.wrid) {
			free(qp->sq.wrid);
			return -1;
		}

		if (qp->max_inlr_sg) {
			qp->inlr_buff.buff = malloc(qp->rq.wqe_cnt * sizeof(*(qp->inlr_buff.buff)));
			if (!qp->inlr_buff.buff) {
				free(qp->sq.wrid);
				free(qp->rq.wrid);
				return -1;
			}
			qp->inlr_buff.len = qp->rq.wqe_cnt;
			qp->inlr_buff.buff[0].sg_list = malloc(qp->rq.wqe_cnt *
							       sizeof(*(qp->inlr_buff.buff->sg_list)) *
							       qp->max_inlr_sg);
			if (!qp->inlr_buff.buff->sg_list) {
				free(qp->sq.wrid);
				free(qp->rq.wrid);
				free(qp->inlr_buff.buff);
				return -1;
			}
			for (i = 1; i < qp->rq.wqe_cnt; i++)
				qp->inlr_buff.buff[i].sg_list = &qp->inlr_buff.buff[0].sg_list[i * qp->max_inlr_sg];
		}
	}

	qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
		(qp->sq.wqe_cnt << qp->sq.wqe_shift);

	if (qp->buf_size) {
		/* compatability support */
		qp_huge_key  = qptype2key(attr->qp_type);
		if (mlx4_use_huge(context, qp_huge_key))
			default_alloc_type = MLX4_ALLOC_TYPE_HUGE;


		mlx4_get_alloc_type(context, MLX4_QP_PREFIX, &alloc_type,
				    default_alloc_type);

		ret = mlx4_alloc_prefered_buf(to_mctx(context), &qp->buf,
				align(qp->buf_size, to_mdev
				(context->device)->page_size),
				to_mdev(context->device)->page_size,
				alloc_type,
				MLX4_QP_PREFIX);

		if (ret) {
			free(qp->sq.wrid);
			free(qp->rq.wrid);
			if (qp->max_inlr_sg) {
				free(qp->inlr_buff.buff[0].sg_list);
				free(qp->inlr_buff.buff);
			}
			return -1;
		}

		memset(qp->buf.buf, 0, qp->buf_size);
		if (qp->rq.wqe_shift > qp->sq.wqe_shift) {
			qp->rq.buf = qp->buf.buf;
			qp->sq.buf = qp->buf.buf + (qp->rq.wqe_cnt << qp->rq.wqe_shift);
		} else {
			qp->rq.buf = qp->buf.buf + (qp->sq.wqe_cnt << qp->sq.wqe_shift);
			qp->sq.buf = qp->buf.buf;
		}

	} else {
		qp->buf.buf = NULL;
	}

	return 0;
}

static uint64_t send_db_to_uar(uintptr_t send_db)
{
	return (send_db - MLX4_SEND_DOORBELL);
}

static uint32_t *uar_to_send_db(uintptr_t uar)
{
	return (uint32_t *)(uar + MLX4_SEND_DOORBELL);
}

static void update_qp_bf_data(struct mlx4_res_domain *res_domain,
			      struct mlx4_qp *qp, struct ibv_context *context)
{
	switch (res_domain->type) {
	case MLX4_RES_DOMAIN_BF_SAFE:
		qp->db_method = MLX4_QP_DB_METHOD_BF;
		break;
	case MLX4_RES_DOMAIN_BF_UNSAFE:
		qp->db_method = MLX4_QP_DB_METHOD_DEDIC_BF;
		break;
	case MLX4_RES_DOMAIN_BF_SINGLE_WC_EVICT:
		if (to_mctx(context)->prefer_bf)
			qp->db_method = MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_PB;
		else
			qp->db_method = MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_NPB;
		break;
	default:
		break;
	}
	qp->bf = &res_domain->send_db->bf;
	qp->sdb = res_domain->send_db->db_addr;
	qp->bf_buf_size = to_mctx(context)->bfs.buf_size;
}

struct ibv_qp *mlx4_exp_create_qp(struct ibv_context *context,
				  struct ibv_exp_qp_init_attr *attr)
{
	struct mlx4_qp		 *qp;
	int			  ret;
	union {
		struct mlx4_create_qp		basic;
		struct mlx4_exp_create_qp	extended;
	} cmd_obj;
	union {
		struct ibv_create_qp_resp	basic;
		struct ibv_exp_create_qp_resp	extended;
	} resp_obj;
	struct mlx4_create_qp_base *cmd = NULL;
	int ext_kernel_cmd = 0;
	struct mlx4_bfs_data *bfs = &to_mctx(context)->bfs;
	int i;
	unsigned char cq_update;
	int thread_safe = !mlx4_single_threaded;
	int db_method_defined = 0;

	memset(&resp_obj, 0, sizeof(resp_obj));
	memset(&cmd_obj, 0, sizeof(cmd_obj));

	if (attr->comp_mask >= IBV_EXP_QP_INIT_ATTR_RESERVED1) {
		errno = ENOSYS;
		return NULL;
	}

	if (attr->comp_mask & IBV_EXP_QP_INIT_ATTR_INL_RECV) {
		if (attr->srq)
			attr->max_inl_recv = 0;
		else
			attr->max_inl_recv = min(attr->max_inl_recv,
						 (to_mctx(context)->max_sge *
						 sizeof(struct mlx4_wqe_data_seg)));
	}

	/* Sanity check QP size before proceeding */
	if (verify_sizes(attr, to_mctx(context)))
		return NULL;

	if (attr->qp_type == IBV_QPT_XRC && attr->recv_cq &&
		attr->cap.max_recv_wr > 0 && mlx4_trace)
		fprintf(stderr, PFX "Warning: Legacy XRC sender should not use a recieve cq\n");

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	qp->qp_cap_cache = 0;
	if (attr->comp_mask >= IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS)
		ext_kernel_cmd = 1;
	if (attr->qp_type == IBV_QPT_XRC_RECV) {
		attr->cap.max_send_wr = qp->sq.wqe_cnt = 0;
	} else {
		if (attr->comp_mask & IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG &&
		    attr->max_atomic_arg != 0) {
			if (attr->max_atomic_arg == 8) {
				qp->is_masked_atomic = 1;
			} else {
				fprintf(stderr, "%s: max_atomic_arg = %d is not valid for mlx4 (use 8 or 0)\n",
					__FUNCTION__, attr->max_atomic_arg);
				errno = EINVAL;
				goto err;
			}
		}

		mlx4_calc_sq_wqe_size(&attr->cap, attr->qp_type, qp);
		/*
		 * We need to leave 2 KB + 1 WQE of headroom in the SQ to
		 * allow HW to prefetch.
		 */
#ifdef MLX4_WQE_FORMAT
		qp->sq_spare_wqes = 0;
#else
		qp->sq_spare_wqes = (2048 >> qp->sq.wqe_shift) + 1;
#endif
		qp->sq.wqe_cnt = align_queue_size(attr->cap.max_send_wr + qp->sq_spare_wqes);
	}

	if (attr->srq || attr->qp_type == IBV_QPT_XRC_SEND ||
	    attr->qp_type == IBV_QPT_XRC_RECV ||
	    attr->qp_type == IBV_QPT_XRC) {
		attr->cap.max_recv_wr = qp->rq.wqe_cnt = attr->cap.max_recv_sge = 0;
		if (attr->comp_mask & IBV_EXP_QP_INIT_ATTR_INL_RECV)
			attr->max_inl_recv = 0;
	} else {
		qp->rq.wqe_cnt = align_queue_size(attr->cap.max_recv_wr);
		if (attr->cap.max_recv_sge < 1)
			attr->cap.max_recv_sge = 1;
		if (attr->cap.max_recv_wr < 1)
			attr->cap.max_recv_wr = 1;
	}

	if (attr->comp_mask & IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS)
		qp->create_flags = attr->exp_create_flags & IBV_EXP_QP_CREATE_MASK;

	if (mlx4_exp_alloc_qp_buf(context, attr, qp))
		goto err;

	mlx4_init_qp_indices(qp);

	qp->sdb = (uint32_t *) (to_mctx(context)->uar + MLX4_SEND_DOORBELL);
	if (attr->comp_mask & IBV_EXP_QP_INIT_ATTR_RES_DOMAIN) {
		struct mlx4_res_domain *rd;

		if (!attr->res_domain) {
			errno = EINVAL;
			goto err_free;
		}
		rd = to_mres_domain(attr->res_domain);
		if (rd->attr.thread_model == IBV_EXP_THREAD_UNSAFE ||
		    rd->attr.thread_model == IBV_EXP_THREAD_SINGLE)
			thread_safe = 0;

		if (rd->send_db) {
			cmd_obj.extended.exp_cmd.uar_virt_add = send_db_to_uar((uintptr_t)rd->send_db->db_addr);
			update_qp_bf_data(rd, qp, context);
			db_method_defined = 1;
		}
	}

	if (mlx4_lock_init(&qp->sq.lock, thread_safe, mlx4_get_locktype()))
		goto err_free;
	if (mlx4_lock_init(&qp->rq.lock, thread_safe, mlx4_get_locktype()))
		goto sq_lock_destroy;

	cmd = (ext_kernel_cmd ?
			&cmd_obj.extended.exp_cmd.base : &cmd_obj.basic.base);

	if (attr->cap.max_recv_sge) {
		qp->db = mlx4_alloc_db(to_mctx(context), MLX4_DB_TYPE_RQ);
		if (!qp->db)
			goto rq_lock_destroy;

		*qp->db = 0;
		cmd->db_addr = (uintptr_t) qp->db;
	} else {
		cmd->db_addr = 0;
	}

	cmd->buf_addr	    = (uintptr_t) qp->buf.buf;
	cmd->log_sq_stride   = qp->sq.wqe_shift;
	for (cmd->log_sq_bb_count = 0;
	     qp->sq.wqe_cnt > 1 << cmd->log_sq_bb_count;
	     ++cmd->log_sq_bb_count)
		; /* nothing */
	cmd->sq_no_prefetch = 0;	/* OK for ABI 2: just a reserved field */
	memset(cmd->reserved, 0, sizeof(cmd->reserved));

	pthread_mutex_lock(&to_mctx(context)->qp_table_mutex);
	ret = ibv_exp_cmd_create_qp(context, &qp->verbs_qp,
				    sizeof(qp->verbs_qp), attr,
				    ext_kernel_cmd ?
				    (void *)&cmd_obj.extended.ibv_cmd :
				    (void *)&cmd_obj.basic.ibv_cmd,
				    ext_kernel_cmd ?
				    sizeof(cmd_obj.extended.ibv_cmd) :
				    sizeof(cmd_obj.basic.ibv_cmd),
				    ext_kernel_cmd ?
				    sizeof(cmd_obj.extended.exp_cmd) :
				    sizeof(cmd_obj.basic.base),
				    ext_kernel_cmd ?
				    (void *)&resp_obj.extended : (void *)&resp_obj.basic,
				    ext_kernel_cmd ?
				    sizeof(resp_obj.extended) :
				    sizeof(resp_obj.basic),
				    0, 0);
	if (ret) {
		errno = ret;
		goto err_rq_db;
	}

	if (qp->max_inlr_sg && (attr->max_inl_recv != (1 << qp->rq.wqe_shift)))
		goto err_destroy;

	if (qp->sq.wqe_cnt || qp->rq.wqe_cnt) {
		ret = mlx4_store_qp(to_mctx(context), qp->verbs_qp.qp.qp_num, qp);
		if (ret)
			goto err_destroy;
	}
	pthread_mutex_unlock(&to_mctx(context)->qp_table_mutex);

	qp->rq.wqe_cnt = attr->cap.max_recv_wr;
	qp->rq.max_gs  = attr->cap.max_recv_sge;

	/* adjust rq maxima to not exceed reported device maxima */
	attr->cap.max_recv_wr = min(to_mctx(context)->max_qp_wr,
					attr->cap.max_recv_wr);
	attr->cap.max_recv_sge = min(to_mctx(context)->max_sge,
					attr->cap.max_recv_sge);

	qp->rq.max_post = attr->cap.max_recv_wr;
	if (attr->qp_type != IBV_QPT_XRC_RECV)
		mlx4_set_sq_sizes(qp, &attr->cap, attr->qp_type);

	qp->doorbell_qpn    = htonl(qp->verbs_qp.qp.qp_num << 8);
	if (attr->sq_sig_all)
		cq_update = MLX4_WQE_CTRL_CQ_UPDATE;
	else
		cq_update = 0;

	/*
	 * The rcrb_flags_tbl is a table to get the right value for the first
	 * byte of srcrb_flags field on the WQE ctrl segment.
	 * The value is derived from the QP sq_sig_all flag and the 4 WR flags
	 * IBV_EXP_SEND_SIGNALED, IBV_EXP_SEND_SOLICITED, IBV_EXP_SEND_IP_CSUM
	 * and IBV_EXP_SEND_TUNNEL.
	 * These flags used as an index to get the required value from the table.
	 * The IBV_EXP_SEND_SIGNALED flag defines first bit of the index the
	 * IBV_EXP_SEND_SOLICITED defines the second bit the IBV_EXP_SEND_IP_CSUM
	 * defines the third bit and IBV_EXP_SEND_TUNNEL the fourth one.
	 * Therefore to calculate the index we can use:
	 *	idx = (exp_send_flags & IBV_EXP_SEND_SIGNALED)/IBV_EXP_SEND_SIGNALED |
	 *	      (exp_send_flags & IBV_EXP_SEND_SOLICITED)/(IBV_EXP_SEND_SOLICITED >> 1) |
	 *	      (exp_send_flags & IBV_EXP_SEND_IP_CSUM)/(IBV_EXP_SEND_IP_CSUM >> 2);
	 *	      (exp_send_flags & IBV_EXP_SEND_TUNNEL)/(IBV_EXP_SEND_TUNNEL >> 3);
	 */
	qp->srcrb_flags_tbl[0] = cq_update;
	qp->srcrb_flags_tbl[1] = MLX4_WQE_CTRL_CQ_UPDATE | cq_update;
	qp->srcrb_flags_tbl[2] = MLX4_WQE_CTRL_SOLICIT | cq_update;
	qp->srcrb_flags_tbl[3] = MLX4_WQE_CTRL_CQ_UPDATE | MLX4_WQE_CTRL_SOLICIT | cq_update;
	qp->srcrb_flags_tbl[4] = MLX4_WQE_CTRL_IP_CSUM | MLX4_WQE_CTRL_TCP_UDP_CSUM | cq_update;
	qp->srcrb_flags_tbl[5] = MLX4_WQE_CTRL_IP_CSUM | MLX4_WQE_CTRL_TCP_UDP_CSUM | MLX4_WQE_CTRL_CQ_UPDATE | cq_update;
	qp->srcrb_flags_tbl[6] = MLX4_WQE_CTRL_IP_CSUM | MLX4_WQE_CTRL_TCP_UDP_CSUM | MLX4_WQE_CTRL_SOLICIT | cq_update;
	qp->srcrb_flags_tbl[7] = MLX4_WQE_CTRL_IP_CSUM | MLX4_WQE_CTRL_TCP_UDP_CSUM | MLX4_WQE_CTRL_CQ_UPDATE | MLX4_WQE_CTRL_SOLICIT | cq_update;
	qp->srcrb_flags_tbl[8] = cq_update;
	qp->srcrb_flags_tbl[9] = MLX4_WQE_CTRL_CQ_UPDATE | cq_update;
	qp->srcrb_flags_tbl[10] = MLX4_WQE_CTRL_SOLICIT | cq_update;
	qp->srcrb_flags_tbl[11] = MLX4_WQE_CTRL_CQ_UPDATE | MLX4_WQE_CTRL_SOLICIT | cq_update;
	qp->srcrb_flags_tbl[12] = MLX4_WQE_CTRL_IP_CSUM | cq_update;
	qp->srcrb_flags_tbl[13] = MLX4_WQE_CTRL_IP_CSUM | MLX4_WQE_CTRL_CQ_UPDATE | cq_update;
	qp->srcrb_flags_tbl[14] = MLX4_WQE_CTRL_IP_CSUM | MLX4_WQE_CTRL_SOLICIT | cq_update;
	qp->srcrb_flags_tbl[15] = MLX4_WQE_CTRL_IP_CSUM | MLX4_WQE_CTRL_CQ_UPDATE | MLX4_WQE_CTRL_SOLICIT | cq_update;

	qp->qp_type = attr->qp_type;

	/* Set default value of cached RX csum flags to 0 */
	qp->cached_rx_csum_flags = 0;
	/* Set transposed_rx_csum_flags to match the cached_rx_csum_flags = 0 */
	qp->transposed_rx_csum_flags = IBV_EXP_CQ_RX_OUTER_IPV6_PACKET;

	if (!db_method_defined && bfs->buf_size == 0) {
		/* not using BF */
		qp->db_method = MLX4_QP_DB_METHOD_DB;
	} else if (!db_method_defined) {
		/*
		 * To gain performance the dedic_bf_free is first tested without taking
		 * the dedic_bf_lock.
		 */
		if (bfs->dedic_bf_free) {
			mlx4_spin_lock(&bfs->dedic_bf_lock);
			for (i = 0 ; i < bfs->num_dedic_bfs; i++) {
				if (!bfs->dedic_bf_used[i]) {
					/* using dedicated BF */
					qp->db_method = MLX4_QP_DB_METHOD_DEDIC_BF;
					qp->bf = (union mlx4_bf *)(&bfs->dedic_bf[i]);
					bfs->dedic_bf_used[i] = 1;
					bfs->dedic_bf_free--;
					break;
				}
			}
			mlx4_spin_unlock(&bfs->dedic_bf_lock);
		}
		if (!qp->bf) {
			/* using common BF */
			if (mlx4_single_threaded)
				qp->db_method = MLX4_QP_DB_METHOD_DEDIC_BF;
			else
				qp->db_method = MLX4_QP_DB_METHOD_BF;
			qp->bf = (union mlx4_bf *)(&bfs->cmn_bf);
		}
		if (qp->db_method == MLX4_QP_DB_METHOD_DEDIC_BF &&
		    mlx4_single_threaded && (wc_auto_evict_size() == 64)) {
			if (to_mctx(context)->prefer_bf)
				qp->db_method = MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_PB;
			else
				qp->db_method = MLX4_QP_DB_METHOD_DEDIC_BF_1_THREAD_WC_EVICT_NPB;
		}
		qp->bf_buf_size = bfs->buf_size;
	}

	qp->model_flags = thread_safe ? MLX4_QP_MODEL_FLAG_THREAD_SAFE : 0;
	mlx4_update_post_send_one(qp);
	qp->pattern = MLX4_QP_PATTERN;

	return &qp->verbs_qp.qp;

err_destroy:
	ibv_cmd_destroy_qp(&qp->verbs_qp.qp);

err_rq_db:
	pthread_mutex_unlock(&to_mctx(context)->qp_table_mutex);
	if (attr->cap.max_recv_sge)
		mlx4_free_db(to_mctx(context), MLX4_DB_TYPE_RQ, qp->db);

rq_lock_destroy:
	mlx4_lock_destroy(&qp->rq.lock);

sq_lock_destroy:
	mlx4_lock_destroy(&qp->sq.lock);

err_free:
	mlx4_dealloc_qp_buf(context, qp);

err:
	free(qp);

	return NULL;
}

int mlx4_exp_query_device(struct ibv_context *context,
			  struct ibv_exp_device_attr *device_attr)
{
	struct ibv_exp_query_device cmd;
	struct ibv_port_attr port_attr;
	uint64_t raw_fw_ver;
	int ret;
	int i;

	ret = ibv_exp_cmd_query_device(context, device_attr, &raw_fw_ver,
				       &cmd, sizeof(cmd));
	if (ret)
		return ret;

	if (device_attr->exp_device_cap_flags & IBV_EXP_DEVICE_CROSS_CHANNEL) {
		device_attr->comp_mask |= IBV_EXP_DEVICE_ATTR_CALC_CAP;
		device_attr->calc_cap.data_types = (1ULL << IBV_EXP_CALC_DATA_TYPE_INT) |
						   (1ULL << IBV_EXP_CALC_DATA_TYPE_UINT) |
						   (1ULL << IBV_EXP_CALC_DATA_TYPE_FLOAT);
		device_attr->calc_cap.data_sizes = (1ULL << IBV_EXP_CALC_DATA_SIZE_64_BIT);
		device_attr->calc_cap.int_ops = (1ULL << IBV_EXP_CALC_OP_ADD) |
						(1ULL << IBV_EXP_CALC_OP_BAND) |
						(1ULL << IBV_EXP_CALC_OP_BXOR) |
						(1ULL << IBV_EXP_CALC_OP_BOR);
		device_attr->calc_cap.uint_ops = device_attr->calc_cap.int_ops;
		device_attr->calc_cap.fp_ops = device_attr->calc_cap.int_ops;
	}
	device_attr->exp_device_cap_flags |= IBV_EXP_DEVICE_MR_ALLOCATE;

	if ((device_attr->comp_mask & IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS) &&
	    (device_attr->exp_device_cap_flags & (IBV_EXP_DEVICE_RX_CSUM_TCP_UDP_PKT |
						  IBV_EXP_DEVICE_RX_CSUM_IP_PKT |
						  IBV_EXP_DEVICE_VXLAN_SUPPORT))) {
		for (i = 0; i < device_attr->phys_port_cnt; i++) {
			ret = mlx4_query_port(context, i + 1, &port_attr);
			if (ret)
				return ret;

			if (port_attr.link_layer != IBV_LINK_LAYER_ETHERNET) {
				device_attr->exp_device_cap_flags &= ~(IBV_EXP_DEVICE_RX_CSUM_TCP_UDP_PKT |
								       IBV_EXP_DEVICE_RX_CSUM_IP_PKT |
								       IBV_EXP_DEVICE_VXLAN_SUPPORT);
				break;
			}
		}
	}

	return __mlx4_query_device(
			raw_fw_ver,
			(struct ibv_device_attr *)device_attr);
}

int mlx4_exp_query_port(struct ibv_context *context, uint8_t port_num,
			struct ibv_exp_port_attr *port_attr)
{
	/* Check that only valid flags were given */
	if (!(port_attr->comp_mask & IBV_EXP_QUERY_PORT_ATTR_MASK1) ||
	    (port_attr->comp_mask & ~IBV_EXP_QUERY_PORT_ATTR_MASKS) ||
	    (port_attr->mask1 & ~IBV_EXP_QUERY_PORT_MASK)) {
		return EINVAL;
	}

	/* Optimize the link type query */
	if (port_attr->comp_mask == IBV_EXP_QUERY_PORT_ATTR_MASK1) {
		if (!(port_attr->mask1 & ~(IBV_EXP_QUERY_PORT_LINK_LAYER |
					   IBV_EXP_QUERY_PORT_CAP_FLAGS))) {
			struct mlx4_context *mctx = to_mctx(context);
			if (port_num <= 0 || port_num > MLX4_PORTS_NUM)
				return EINVAL;
			if (mctx->port_query_cache[port_num - 1].valid) {
				if (port_attr->mask1 &
				    IBV_EXP_QUERY_PORT_LINK_LAYER)
					port_attr->link_layer =
						mctx->
						port_query_cache[port_num - 1].
						link_layer;
				if (port_attr->mask1 &
				    IBV_EXP_QUERY_PORT_CAP_FLAGS)
					port_attr->port_cap_flags =
						mctx->
						port_query_cache[port_num - 1].
						caps;
				return 0;
			}
		}
		if (port_attr->mask1 & IBV_EXP_QUERY_PORT_STD_MASK) {
			return mlx4_query_port(context, port_num,
					       &port_attr->port_attr);
		}
	}

	return EOPNOTSUPP;
}

struct ibv_ah *mlx4_exp_create_ah(struct ibv_pd *pd,
				  struct ibv_exp_ah_attr *attr_ex)
{
	struct ibv_exp_port_attr port_attr;
	struct ibv_ah *ah;
	struct mlx4_ah *mah;

	port_attr.comp_mask = IBV_EXP_QUERY_PORT_ATTR_MASK1;
	port_attr.mask1 = IBV_EXP_QUERY_PORT_LINK_LAYER;

	if (ibv_exp_query_port(pd->context, attr_ex->port_num, &port_attr))
		return NULL;

	ah = mlx4_create_ah_common(pd, (struct ibv_ah_attr *)attr_ex,
				   port_attr.link_layer);

	if (NULL == ah)
		return NULL;

	mah = to_mah(ah);

	/* If vlan was given, check that we could use it */
	if (attr_ex->comp_mask & IBV_EXP_AH_ATTR_VID &&
	    attr_ex->vid <= 0xfff &&
	    (0 == attr_ex->ll_address.len ||
	     !(attr_ex->comp_mask & IBV_EXP_AH_ATTR_LL)))
		goto err;

	/* ll_address.len == 0 means no ll address given */
	if (attr_ex->comp_mask & IBV_EXP_AH_ATTR_LL &&
	    0 != attr_ex->ll_address.len) {
		if (LL_ADDRESS_ETH != attr_ex->ll_address.type ||
		    port_attr.link_layer != IBV_LINK_LAYER_ETHERNET)
			/* mlx4 provider currently only support ethernet
			 * extensions */
			goto err;

		/* link layer is ethernet */
		if (6 != attr_ex->ll_address.len ||
		    NULL == attr_ex->ll_address.address)
			goto err;

		memcpy(mah->mac, attr_ex->ll_address.address,
		       attr_ex->ll_address.len);

		if (attr_ex->comp_mask & IBV_EXP_AH_ATTR_VID &&
		    attr_ex->vid <= 0xfff) {
				mah->av.port_pd |= htonl(1 << 29);
				mah->vlan = attr_ex->vid |
					((attr_ex->sl & 7) << 13);
		}
	}

	return ah;

err:
	free(ah);
	return NULL;
}

static struct mlx4_send_db_data *allocate_send_db(struct mlx4_context *ctx)
{
	struct mlx4_device *dev = to_mdev(ctx->ibv_ctx.device);
	struct mlx4_send_db_data *send_db = NULL;
	unsigned int uar_idx;
	void *uar;
	void *bfs;
	int i;

	if (!ctx->max_ctx_res_domain || !ctx->bfs.buf_size) {
		errno = EINVAL;
		return NULL;
	}

	mlx4_spin_lock(&ctx->send_db_lock);
	if (!list_empty(&ctx->send_db_list)) {
		send_db = list_entry(ctx->send_db_list.next, struct mlx4_send_db_data, list);
		list_del(&send_db->list);
	}
	mlx4_spin_unlock(&ctx->send_db_lock);

	if (!send_db) {
		/* Fill up more send_db objects */
		mlx4_spin_lock(&ctx->send_db_lock);
		if ((ctx->send_db_num_uars + 1) * ctx->bf_regs_per_page >= ctx->max_ctx_res_domain) {
			mlx4_spin_unlock(&ctx->send_db_lock);
			errno = ENOMEM;
			return NULL;
		}
		uar_idx = ctx->send_db_num_uars;
		ctx->send_db_num_uars++;
		mlx4_spin_unlock(&ctx->send_db_lock);

		uar = mmap(NULL, dev->page_size, PROT_WRITE, MAP_SHARED,
			   ctx->ibv_ctx.cmd_fd,
			   dev->page_size * (MLX4_IB_EXP_MMAP_EXT_UAR_PAGE |
					     (uar_idx << MLX4_MMAP_CMD_BITS)));
		if (uar == MAP_FAILED)
			return NULL;
		bfs = mmap(NULL, dev->page_size, PROT_WRITE, MAP_SHARED,
			   ctx->ibv_ctx.cmd_fd,
			   dev->page_size * (MLX4_IB_EXP_MMAP_EXT_BLUE_FLAME_PAGE |
					     (uar_idx << MLX4_MMAP_CMD_BITS)));
		if (bfs == MAP_FAILED) {
			munmap(uar, dev->page_size);
			return NULL;
		}
		mlx4_spin_lock(&ctx->send_db_lock);
		for (i = 0; i < ctx->bf_regs_per_page; i++) {
			send_db = calloc(1, sizeof(*send_db));
			if (!send_db) {
				if (i)
					break;
				mlx4_spin_unlock(&ctx->send_db_lock);
				errno = ENOMEM;
				return NULL;
			}

			mlx4_lock_init(&send_db->bf.cmn.lock,
				       !mlx4_single_threaded,
				       mlx4_get_locktype());

			send_db->db_addr = uar_to_send_db((uintptr_t)uar);

			/* Allocate a pair of blue-flames to toggle sends between them */
			send_db->bf.cmn.address = bfs + (i * ctx->bfs.buf_size * 2);
			list_add(&send_db->list, &ctx->send_db_list);
		}

		/* Return the last send_db object to the caller */
		list_del(&send_db->list);
		mlx4_spin_unlock(&ctx->send_db_lock);
	}

	return send_db;
}

static void free_send_db(struct mlx4_context *ctx,
			 struct mlx4_send_db_data *send_db)
{
	mlx4_spin_lock(&ctx->send_db_lock);
	list_add(&send_db->list, &ctx->send_db_list);
	mlx4_spin_unlock(&ctx->send_db_lock);
}

struct ibv_exp_res_domain *mlx4_exp_create_res_domain(struct ibv_context *context,
						      struct ibv_exp_res_domain_init_attr *attr)
{
	struct mlx4_context *ctx = to_mctx(context);
	struct mlx4_res_domain *res_domain;

	if (attr->comp_mask >= IBV_EXP_RES_DOMAIN_RESERVED) {
		errno = EINVAL;
		return NULL;
	}

	res_domain = calloc(1, sizeof(*res_domain));
	if (!res_domain) {
		errno = ENOMEM;
		return NULL;
	}

	res_domain->ibv_res_domain.context = context;

	/* set default values */
	res_domain->attr.thread_model = IBV_EXP_THREAD_SAFE;
	res_domain->attr.msg_model = IBV_EXP_MSG_DEFAULT;
	/* get requested valid values */
	if (attr->comp_mask & IBV_EXP_RES_DOMAIN_THREAD_MODEL)
		res_domain->attr.thread_model = attr->thread_model;
	if (attr->comp_mask & IBV_EXP_RES_DOMAIN_MSG_MODEL)
		res_domain->attr.msg_model = attr->msg_model;
	res_domain->attr.comp_mask = IBV_EXP_RES_DOMAIN_THREAD_MODEL |
				     IBV_EXP_RES_DOMAIN_MSG_MODEL;
	/*
	 * Allocate BF for every resource domain since BF is improving
	 * both BW and latency of single message.
	 */
	res_domain->send_db = allocate_send_db(ctx);

	/* define resource domain type */
	if (!res_domain->send_db) {
		if (res_domain->attr.msg_model == IBV_EXP_MSG_FORCE_LOW_LATENCY)
			/*
			 * Fail in case user asked for force low-latency
			 * resource-domain but we can't allocate
			 * dedicated BF.
			 */
			goto err;
		else
			/*
			 * Dedicated BF is not allocated for the
			 * resource-domain.
			 */
			res_domain->type = MLX4_RES_DOMAIN_BF_NONE;
	} else {
		/*
		 * In case dedicated BF allocated set the
		 * resource-domain type according to the
		 * thread-model
		 */
		switch (res_domain->attr.thread_model) {
		case IBV_EXP_THREAD_SAFE:
			res_domain->type = MLX4_RES_DOMAIN_BF_SAFE;
			break;
		case IBV_EXP_THREAD_UNSAFE:
			res_domain->type = MLX4_RES_DOMAIN_BF_UNSAFE;
			break;
		case IBV_EXP_THREAD_SINGLE:
			if (wc_auto_evict_size() == 64)
				res_domain->type = MLX4_RES_DOMAIN_BF_SINGLE_WC_EVICT;
			else
				res_domain->type = MLX4_RES_DOMAIN_BF_UNSAFE;
			break;
		}
	}

	return &res_domain->ibv_res_domain;

err:
	free(res_domain);

	return NULL;
}

int mlx4_exp_destroy_res_domain(struct ibv_context *context,
				struct ibv_exp_res_domain *res_dom,
				struct ibv_exp_destroy_res_domain_attr *attr)
{
	struct mlx4_res_domain *res_domain = to_mres_domain(res_dom);

	if (res_domain->send_db)
		free_send_db(to_mctx(context), res_domain->send_db);

	free(res_domain);

	return 0;
}

void *mlx4_exp_query_intf(struct ibv_context *context, struct ibv_exp_query_intf_params *params,
			  enum ibv_exp_query_intf_status *status)
{
	void *family = NULL;
	struct mlx4_qp *qp;
	struct mlx4_cq *cq;

	*status = IBV_EXP_INTF_STAT_OK;

	if (!params->obj) {
		errno = EINVAL;
		*status = IBV_EXP_INTF_STAT_INVAL_OBJ;

		return NULL;
	}

	if (params->intf_version > MLX4_MAX_FAMILY_VER) {
		*status = IBV_EXP_INTF_STAT_VERSION_NOT_SUPPORTED;

		return NULL;
	}

	switch (params->intf) {
	case IBV_EXP_INTF_QP_BURST:
		qp = to_mqp(params->obj);
		if (qp->pattern == MLX4_QP_PATTERN) {
			family = mlx4_get_qp_burst_family(qp, params, status);
			if (*status != IBV_EXP_INTF_STAT_OK) {
				fprintf(stderr, PFX "Failed to get QP burst family\n");
				errno = EINVAL;
			}
		} else {
			fprintf(stderr, PFX "Warning: non-valid QP passed to query interface\n");
			*status = IBV_EXP_INTF_STAT_INVAL_OBJ;
			errno = EINVAL;
		}
		break;

	case IBV_EXP_INTF_CQ:
		cq = to_mcq(params->obj);
		if (cq->pattern == MLX4_CQ_PATTERN) {
			family = (void *)mlx4_get_poll_cq_family(cq, params, status);
		} else {
			fprintf(stderr, PFX "Warning: non-valid CQ passed to query interface\n");
			*status = IBV_EXP_INTF_STAT_INVAL_OBJ;
			errno = EINVAL;
		}
		break;

	default:
		*status = IBV_EXP_INTF_STAT_INTF_NOT_SUPPORTED;
		errno = EINVAL;
	}

	return family;
}

int mlx4_exp_release_intf(struct ibv_context *context, void *intf,
			  struct ibv_exp_release_intf_params *params)
{
	return 0;
}
