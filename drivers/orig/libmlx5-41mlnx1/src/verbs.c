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
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "mlx5.h"
#include "mlx5-abi.h"
#include "wqe.h"

int mlx5_single_threaded = 0;
int mlx5_use_mutex;

static void __mlx5_query_device(uint64_t raw_fw_ver,
				struct ibv_device_attr *attr)
{
	unsigned major, minor, sub_minor;

	major     = (raw_fw_ver >> 32) & 0xffff;
	minor     = (raw_fw_ver >> 16) & 0xffff;
	sub_minor = raw_fw_ver & 0xffff;

	snprintf(attr->fw_ver, sizeof attr->fw_ver,
		 "%d.%d.%04d", major, minor, sub_minor);
}

int mlx5_query_device(struct ibv_context *context,
		      struct ibv_device_attr *attr)
{
	struct ibv_query_device cmd;
	uint64_t raw_fw_ver;
	int err;

	read_init_vars(to_mctx(context));
	err = ibv_cmd_query_device(context, attr, &raw_fw_ver, &cmd, sizeof(cmd));
	if (err)
		return err;

	__mlx5_query_device(raw_fw_ver, attr);

	 return err;
}

int mlx5_query_device_ex(struct ibv_context *context,
			 const struct ibv_query_device_ex_input *input,
			 struct ibv_device_attr_ex *attr,
			 size_t attr_size)
{
	struct mlx5_query_device_ex_resp resp;
	struct mlx5_query_device_ex cmd;
	struct ibv_device_attr *a;
	uint64_t raw_fw_ver;
	unsigned sub_minor;
	unsigned major;
	unsigned minor;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	memset(&resp, 0, sizeof(resp));
	err = ibv_cmd_query_device_ex(context, input, attr, attr_size,
				      &raw_fw_ver,
				      &cmd.ibv_cmd, sizeof(cmd.ibv_cmd), sizeof(cmd),
				      &resp.ibv_resp, sizeof(resp.ibv_resp),
				      sizeof(resp.ibv_resp));
	if (err)
		return err;

	major     = (raw_fw_ver >> 32) & 0xffff;
	minor     = (raw_fw_ver >> 16) & 0xffff;
	sub_minor = raw_fw_ver & 0xffff;
	a = &attr->orig_attr;
	snprintf(a->fw_ver, sizeof(a->fw_ver), "%d.%d.%04d",
		 major, minor, sub_minor);

	return 0;
}

int mlx5_exp_query_device(struct ibv_context *context,
			 struct ibv_exp_device_attr *attr)
{
	struct ibv_exp_query_device cmd;
	struct mlx5_context *ctx = to_mctx(context);
	uint64_t raw_fw_ver;
	int err;

	err = ibv_exp_cmd_query_device(context, attr, &raw_fw_ver,
				       &cmd, sizeof(cmd));
	if (err)
		return err;

	__mlx5_query_device(raw_fw_ver, (struct ibv_device_attr *)attr);

	attr->exp_device_cap_flags |= IBV_EXP_DEVICE_MR_ALLOCATE;
	if (attr->exp_device_cap_flags & IBV_EXP_DEVICE_CROSS_CHANNEL) {
		attr->comp_mask |= IBV_EXP_DEVICE_ATTR_CALC_CAP;
		attr->calc_cap.data_types =
				(1ULL << IBV_EXP_CALC_DATA_TYPE_INT) |
				(1ULL << IBV_EXP_CALC_DATA_TYPE_UINT) |
				(1ULL << IBV_EXP_CALC_DATA_TYPE_FLOAT);
		attr->calc_cap.data_sizes =
				(1ULL << IBV_EXP_CALC_DATA_SIZE_64_BIT);
		attr->calc_cap.int_ops = (1ULL << IBV_EXP_CALC_OP_ADD) |
				(1ULL << IBV_EXP_CALC_OP_BAND) |
				(1ULL << IBV_EXP_CALC_OP_BXOR) |
				(1ULL << IBV_EXP_CALC_OP_BOR);
		attr->calc_cap.uint_ops = attr->calc_cap.int_ops;
		attr->calc_cap.fp_ops = attr->calc_cap.int_ops;
	}
	if (ctx->cc.buf)
		attr->exp_device_cap_flags |= IBV_EXP_DEVICE_DC_INFO;

	if (attr->comp_mask & IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS)
		attr->exp_device_cap_flags &= (~IBV_EXP_DEVICE_VXLAN_SUPPORT);

	if (attr->comp_mask & IBV_EXP_DEVICE_ATTR_MP_RQ)
		/* Lib supports MP-RQ only for RAW_ETH QPs reset other
		 * QP types supported by kernel
		 */
		attr->mp_rq_caps.supported_qps &= IBV_EXP_QPT_RAW_PACKET;


	if (attr->comp_mask & IBV_EXP_DEVICE_ATTR_MP_RQ) {
		/* Update kernel caps to mp_rq caps supported by lib */
		attr->mp_rq_caps.allowed_shifts &= MLX5_MP_RQ_SUPPORTED_SHIFTS;
		attr->mp_rq_caps.supported_qps &= MLX5_MP_RQ_SUPPORTED_QPT;
		if (attr->mp_rq_caps.max_single_stride_log_num_of_bytes > MLX5_MP_RQ_MAX_LOG_STRIDE_SIZE)
			attr->mp_rq_caps.max_single_stride_log_num_of_bytes = MLX5_MP_RQ_MAX_LOG_STRIDE_SIZE;
		if (attr->mp_rq_caps.max_single_wqe_log_num_of_strides > MLX5_MP_RQ_MAX_LOG_NUM_STRIDES)
			attr->mp_rq_caps.max_single_wqe_log_num_of_strides = MLX5_MP_RQ_MAX_LOG_NUM_STRIDES;
	}

	return err;
}

int mlx5_query_port(struct ibv_context *context, uint8_t port,
		     struct ibv_port_attr *attr)
{
	struct mlx5_context *mctx = to_mctx(context);
	struct ibv_query_port cmd;
	int err;

	read_init_vars(mctx);
	err = ibv_cmd_query_port(context, port, attr, &cmd, sizeof cmd);

	if (!err && port <= mctx->num_ports && port > 0) {
		if (!mctx->port_query_cache[port - 1].valid) {
			mctx->port_query_cache[port - 1].link_layer =
				attr->link_layer;
			mctx->port_query_cache[port - 1].caps =
				attr->port_cap_flags;
			mctx->port_query_cache[port - 1].valid = 1;
		}
	}

	return err;
}

int mlx5_exp_query_port(struct ibv_context *context, uint8_t port_num,
			struct ibv_exp_port_attr *port_attr)
{
	struct mlx5_context *mctx = to_mctx(context);

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
			if (port_num <= 0 || port_num > mctx->num_ports)
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
			return mlx5_query_port(context, port_num,
					       &port_attr->port_attr);
		}
	}

	return EOPNOTSUPP;

}

struct ibv_pd *mlx5_alloc_pd(struct ibv_context *context)
{
	struct ibv_alloc_pd       cmd;
	struct mlx5_alloc_pd_resp resp;
	struct mlx5_pd		 *pd;

	read_init_vars(to_mctx(context));
	pd = calloc(1, sizeof *pd);
	if (!pd)
		return NULL;

	if (ibv_cmd_alloc_pd(context, &pd->ibv_pd, &cmd, sizeof cmd,
			     &resp.ibv_resp, sizeof(resp)))
		goto err;

	pd->pdn = resp.pdn;


	if (mlx5_init_implicit_lkey(&pd->r_ilkey, IBV_EXP_ACCESS_ON_DEMAND) ||
		mlx5_init_implicit_lkey(&pd->w_ilkey, IBV_EXP_ACCESS_ON_DEMAND |
					IBV_EXP_ACCESS_LOCAL_WRITE))
		goto err;

	return &pd->ibv_pd;

err:
	free(pd);
	return NULL;
}

int mlx5_free_pd(struct ibv_pd *pd)
{
	struct mlx5_pd *mpd = to_mpd(pd);
	int ret;

	/* TODO: Better handling of destruction failure due to resources
	* opened. At the moment, we might seg-fault here.*/
	mlx5_destroy_implicit_lkey(&mpd->r_ilkey);
	mlx5_destroy_implicit_lkey(&mpd->w_ilkey);
	if (mpd->remote_ilkey) {
		mlx5_destroy_implicit_lkey(mpd->remote_ilkey);
		mpd->remote_ilkey = NULL;
	}

	ret = ibv_cmd_dealloc_pd(pd);
	if (ret)
		return ret;

	free(mpd);
	return 0;
}

static void *alloc_buf(struct mlx5_mr *mr,
		       struct ibv_pd *pd,
		       size_t length,
		       void *contig_addr)
{
	size_t alloc_length;
	int force_anon = 0;
	int force_contig = 0;
	enum mlx5_alloc_type alloc_type;
	int page_size = to_mdev(pd->context->device)->page_size;
	int err;

	mlx5_get_alloc_type(pd->context, MLX5_MR_PREFIX, &alloc_type, MLX5_ALLOC_TYPE_ALL);

	if (alloc_type == MLX5_ALLOC_TYPE_CONTIG)
		force_contig = 1;
	else if (alloc_type == MLX5_ALLOC_TYPE_ANON)
		force_anon = 1;

	if (force_anon) {
		err = mlx5_alloc_buf(&mr->buf, align(length, page_size),
				     page_size);
		if (err)
			return NULL;

		return mr->buf.buf;
	}

	alloc_length = (contig_addr ? length : align(length, page_size));

	err = mlx5_alloc_buf_contig(to_mctx(pd->context), &mr->buf,
				    alloc_length, page_size, MLX5_MR_PREFIX,
				    contig_addr);
	if (!err)
		return contig_addr ? contig_addr : mr->buf.buf;

	if (force_contig || contig_addr)
		return NULL;

	err = mlx5_alloc_buf(&mr->buf, align(length, page_size),
			     page_size);
	if (err)
		return NULL;

	return mr->buf.buf;
}

struct ibv_mr *mlx5_exp_reg_mr(struct ibv_exp_reg_mr_in *in)
{
	struct mlx5_mr *mr;
	struct ibv_exp_reg_mr cmd;
	int ret;
	int is_contig;

	if ((in->comp_mask > IBV_EXP_REG_MR_RESERVED - 1) ||
	    (in->exp_access > IBV_EXP_ACCESS_RESERVED - 1)) {
		errno = EINVAL;
		return NULL;
	}

	if ((in->comp_mask & IBV_EXP_REG_MR_DM) &&
	    ((!in->dm) || (in->exp_access & ~MLX5_DM_ALLOWED_ACCESS))) {
		errno = EINVAL;
		return NULL;
	}

	if (!to_mctx(in->pd->context)->implicit_odp &&
	    in->addr == 0 && in->length == MLX5_WHOLE_ADDR_SPACE &&
	    (in->exp_access & IBV_EXP_ACCESS_ON_DEMAND))
		return mlx5_alloc_whole_addr_mr(in);

	if ((in->exp_access &
	    (IBV_EXP_ACCESS_ON_DEMAND | IBV_EXP_ACCESS_RELAXED)) ==
	    (IBV_EXP_ACCESS_ON_DEMAND | IBV_EXP_ACCESS_RELAXED)) {
		struct ibv_mr *ibv_mr = NULL;
		struct mlx5_pd *mpd = to_mpd(in->pd);
		struct mlx5_implicit_lkey *implicit_lkey =
			mlx5_get_implicit_lkey(mpd, in->exp_access);
		struct ibv_exp_prefetch_attr prefetch_attr = {
			.flags = in->exp_access &
				(IBV_ACCESS_LOCAL_WRITE |
				 IBV_ACCESS_REMOTE_WRITE |
				 IBV_ACCESS_REMOTE_READ) ?
				IBV_EXP_PREFETCH_WRITE_ACCESS : 0,
			.addr = in->addr,
			.length = in->length,
			.comp_mask = 0,
		};

		if (!implicit_lkey)
			return NULL;
		errno = mlx5_get_real_mr_from_implicit_lkey(mpd, implicit_lkey,
							    (uintptr_t)in->addr,
							    in->length,
							    &ibv_mr);
		if (errno)
			return NULL;

		/* Prefetch the requested range */
		ibv_exp_prefetch_mr(ibv_mr, &prefetch_attr);

		return ibv_mr;
	}

	mr = calloc(1, sizeof(*mr));
	if (!mr)
		return NULL;

	/*
	 * if addr is NULL and IBV_EXP_ACCESS_ALLOCATE_MR is set,
	 * the library allocates contiguous memory
	 */

	/* Need valgrind exception here due to compiler optimization problem */
	VALGRIND_MAKE_MEM_DEFINED(&in->create_flags, sizeof(in->create_flags));
	is_contig = (!in->addr && (in->exp_access & IBV_EXP_ACCESS_ALLOCATE_MR)) ||
		    ((in->comp_mask & IBV_EXP_REG_MR_CREATE_FLAGS) &&
		     (in->create_flags & IBV_EXP_REG_MR_CREATE_CONTIG));

	if (is_contig) {
		in->addr = alloc_buf(mr, in->pd, in->length, in->addr);
		if (!in->addr) {
			free(mr);
			return NULL;
		}

		mr->alloc_flags |= IBV_EXP_ACCESS_ALLOCATE_MR;
		/*
		 * set the allocated address for the verbs consumer
		 */
		mr->ibv_mr.addr = in->addr;
	}

	/* We should store the ODP or DM type of the MR to avoid
	 * calling "ibv_dofork_range" when invoking ibv_dereg_mr
	 */
	if (in->exp_access & IBV_EXP_ACCESS_ON_DEMAND)
		mr->type = MLX5_ODP_MR;

	if (in->comp_mask & IBV_EXP_REG_MR_DM)
		mr->type = MLX5_DM_MR;
	{
		struct ibv_exp_reg_mr_resp resp;

		ret = ibv_cmd_exp_reg_mr(in,
				     (uintptr_t) in->addr,
				     &(mr->ibv_mr),
				     &cmd, sizeof(cmd),
				     &resp, sizeof(resp));
	}
	if (ret) {
		if ((mr->alloc_flags & IBV_EXP_ACCESS_ALLOCATE_MR)) {
			if (mr->buf.type == MLX5_ALLOC_TYPE_CONTIG)
				mlx5_free_buf_contig(to_mctx(in->pd->context),
						     &mr->buf);
			else
				mlx5_free_buf(&(mr->buf));
		}
		free(mr);
		return NULL;
	}

	return &mr->ibv_mr;
}

struct ibv_mr *mlx5_reg_mr(struct ibv_pd *pd, void *addr,
			   size_t length, int access)
{
	struct ibv_reg_mr_resp resp = {};
	struct ibv_reg_mr cmd = {};
	struct mlx5_mr *mr;
	int ret;

	mr = calloc(1, sizeof(*mr));
	if (!mr)
		return NULL;

	ret = ibv_cmd_reg_mr(pd, addr, length,
			     (uintptr_t)addr, access,
			     &(mr->ibv_mr),
			     &cmd, sizeof(cmd),
			     &resp, sizeof(resp));
	if (ret) {
		free(mr);
		return NULL;
	}

	mr->alloc_flags = access;
	return &mr->ibv_mr;
}

int mlx5_rereg_mr(struct ibv_mr *ibmr, int flags, struct ibv_pd *pd, void *addr,
		  size_t length, int access)
{
	struct ibv_rereg_mr cmd;
	struct ibv_rereg_mr_resp resp;

	if (flags & IBV_REREG_MR_KEEP_VALID)
		return ENOTSUP;

	return ibv_cmd_rereg_mr(ibmr, flags, addr, length, (uintptr_t)addr,
				access, pd, &cmd, sizeof(cmd), &resp,
				sizeof(resp));
}

int mlx5_dereg_mr(struct ibv_mr *ibmr)
{
	int ret;
	struct mlx5_mr *mr = to_mmr(ibmr);

	if (ibmr->lkey == ODP_GLOBAL_R_LKEY ||
	    ibmr->lkey == ODP_GLOBAL_W_LKEY) {
		mlx5_dealloc_whole_addr_mr(ibmr);
		return 0;
	}

	if (mr->alloc_flags & IBV_EXP_ACCESS_RELAXED)
		return 0;

	if (mr->alloc_flags & IBV_EXP_ACCESS_NO_RDMA)
		goto free_mr;

	ret = ibv_cmd_dereg_mr(ibmr);
	if (ret)
		return ret;

free_mr:
	if ((mr->alloc_flags & IBV_EXP_ACCESS_ALLOCATE_MR)) {
		if (mr->buf.type == MLX5_ALLOC_TYPE_CONTIG)
			mlx5_free_buf_contig(to_mctx(ibmr->context), &mr->buf);
		else
			mlx5_free_buf(&(mr->buf));
	}

	free(mr);
	return 0;
}

int mlx5_prefetch_mr(struct ibv_mr *mr, struct ibv_exp_prefetch_attr *attr)
{

	struct mlx5_pd *pd = to_mpd(mr->pd);

	if (attr->comp_mask >= IBV_EXP_PREFETCH_MR_RESERVED)
		return EINVAL;


	switch (mr->lkey) {
	case ODP_GLOBAL_R_LKEY:
		return mlx5_prefetch_implicit_lkey(pd, &pd->r_ilkey,
						   (unsigned long)attr->addr,
						   attr->length, attr->flags);
	case ODP_GLOBAL_W_LKEY:
		return mlx5_prefetch_implicit_lkey(pd, &pd->w_ilkey,
						   (unsigned long)attr->addr,
						   attr->length, attr->flags);
	default:
		break;
	}

	return ibv_cmd_exp_prefetch_mr(mr, attr);
}

struct ibv_mw *mlx5_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type)
{
	struct ibv_mw *mw;
	struct ibv_alloc_mw cmd;
	struct ibv_alloc_mw_resp resp;
	int ret;

	mw = malloc(sizeof(*mw));
	if (!mw)
		return NULL;

	memset(mw, 0, sizeof(*mw));

	ret = ibv_cmd_alloc_mw(pd, type, mw, &cmd, sizeof(cmd), &resp,
			       sizeof(resp));
	if (ret) {
		free(mw);
		return NULL;
	}

	return mw;
}

int mlx5_dealloc_mw(struct ibv_mw *mw)
{
	int ret;
	struct ibv_dealloc_mw cmd;

	ret = ibv_cmd_dealloc_mw(mw, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	free(mw);
	return 0;
}

int mlx5_round_up_power_of_two(long long sz)
{
	long long ret;

	for (ret = 1; ret < sz; ret <<= 1)
		; /* nothing */

	if (ret > INT_MAX) {
		fprintf(stderr, "%s: roundup overflow\n", __func__);
		return -ENOMEM;
	}

	return (int)ret;
}

static int align_queue_size(long long req)
{
	return mlx5_round_up_power_of_two(req);
}

static int get_cqe_size(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];
	struct mlx5_context *ctx = to_mctx(context);
	int size = ctx->cache_line_size;

	size = max(size, 64);
	size = min(size, 128);

	if (!ibv_exp_cmd_getenv(context, "MLX5_CQE_SIZE", env, sizeof(env)))
		size = atoi(env);

	switch (size) {
	case 64:
	case 128:
		return size;

	default:
		return -EINVAL;
	}
}

static int rwq_sig_enabled(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(context, "MLX5_RWQ_SIGNATURE", env, sizeof(env)))
		return 1;

	return 0;
}

static int qp_sig_enabled(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(context, "MLX5_QP_SIGNATURE", env, sizeof(env)))
		return 1;

	return 0;
}

static int check_peer_direct(struct mlx5_context *ctx,
			     struct ibv_exp_peer_direct_attr *attrs,
			     unsigned dbg_mask, unsigned long caps)
{
#ifdef MLX5_DEBUG
	FILE *fp = ctx->dbg_fp;
#endif
	if (!attrs) {
		mlx5_dbg(fp, dbg_mask, "no peer direct attrs\n");
		errno = EINVAL;
		return -1;
	}
	if (attrs->comp_mask != IBV_EXP_PEER_DIRECT_VERSION ||
	    attrs->version != 1) {
		mlx5_dbg(fp, dbg_mask, "peer direct version mismatch\n");
		errno = EINVAL;
		return -1;
	}

	if (!attrs->unregister_va != !attrs->register_va) {
		mlx5_dbg(fp, dbg_mask, "inconsistent peer direct register hooks\n");
		mlx5_dbg(fp, dbg_mask, "register_va:%p unregister_va:%p\n",
			 attrs->register_va, attrs->unregister_va);
		errno = EINVAL;
		return -1;
	}

	if (!attrs->buf_release != !attrs->buf_alloc) {
		mlx5_dbg(fp, dbg_mask, "inconsistent peer direct alloc hooks\n");
		mlx5_dbg(fp, dbg_mask, "buf_alloc:%p buf_release:%p\n",
			 attrs->buf_alloc, attrs->buf_release);
		errno = EINVAL;
		return -1;
	}

	if ((attrs->caps & caps) != caps) {
		mlx5_dbg(fp, dbg_mask, "insufficent peer direct caps\n");
		mlx5_dbg(fp, dbg_mask, "expected:%lx got:%lx\n",
			 caps, attrs->caps);
		errno = EINVAL;
		return 0;
	}

	return 0;
}

static inline void free_peer_va(struct mlx5_qp *qp)
{
	int i;

	if (!qp->peer_ctx->unregister_va)
		return;
	for (i = 0; i < sizeof(qp->peer_va_ids)/sizeof(uint64_t); i++) {
		if (!qp->peer_va_ids[i])
			continue;
		qp->peer_ctx->unregister_va(qp->peer_va_ids[i],
					    qp->peer_ctx->peer_id);
	}
}

enum {
	EXP_CREATE_CQ_SUPPORTED_FLAGS = IBV_EXP_CQ_CREATE_CROSS_CHANNEL |
					IBV_EXP_CQ_TIMESTAMP |
					IBV_EXP_CQ_COMPRESSED_CQE
};

struct ibv_exp_dm *mlx5_exp_alloc_dm(struct ibv_context *context,
				     struct ibv_exp_alloc_dm_attr *dm_attr)
{
	int page_size = to_mdev(context->device)->page_size;
	struct ibv_exp_alloc_dm_resp resp = {};
	struct ibv_exp_alloc_dm cmd = {};
	struct mlx5_dm *dm;
	void *start_addr;
	size_t act_size;
	int ret;

	if (dm_attr->length > to_mctx(context)->max_dm_size) {
		errno = EINVAL;
		return NULL;
	}

	dm =  calloc(1, sizeof(*dm));
	if (!dm) {
		errno = ENOMEM;
		return NULL;
	}

	act_size = align(dm_attr->length, page_size);
	start_addr = mmap(NULL, act_size, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (start_addr == MAP_FAILED) {
		errno = ENOMEM;
		goto err_mmap;
	}

	if (ibv_dontfork_range(start_addr, act_size)) {
		errno = EFAULT;
		goto err_dontfork;
	}

	ret = ibv_exp_cmd_alloc_dm(context, dm_attr, &dm->ibdm, start_addr,
				   &cmd, sizeof(cmd), sizeof(cmd),
				   &resp, sizeof(resp), sizeof(resp));

	if (ret)
		goto err_alloc_dm;

	dm->start_va = start_addr + resp.start_offset;
	dm->length = dm_attr->length;

	return &dm->ibdm;

err_alloc_dm:
	ibv_dofork_range(start_addr, act_size);

err_dontfork:
	munmap(start_addr, act_size);

err_mmap:
	free(dm);

	return NULL;
}

int mlx5_exp_free_dm(struct ibv_exp_dm *ibdm)
{
	struct mlx5_dm *dm = to_mdm(ibdm);
	struct mlx5_device *mdev = to_mdev(ibdm->context->device);
	uint64_t page_mask = mdev->page_size - 1;
	uint64_t unmap_va = (uint64_t)(uintptr_t)dm->start_va & ~page_mask;
	size_t act_size;
	int ret;

	ret = ibv_exp_cmd_free_dm(ibdm);

	act_size = align(dm->length, mdev->page_size);

	if (!ret) {
		ibv_dofork_range((void *)(uintptr_t)unmap_va, act_size);
		munmap((void *)(uintptr_t)unmap_va, act_size);
		free(dm);
	}

	return ret;
}

int mlx5_exp_memcpy_dm(struct ibv_exp_dm *ibdm,
		       struct ibv_exp_memcpy_dm_attr *attr)
{
	struct mlx5_dm *dm = to_mdm(ibdm);
	uint64_t dm_va = (uint64_t)(uintptr_t)dm->start_va + attr->dm_offset;
	uint64_t copy_size = attr->length;
	uint64_t offset = 0;
	uint32_t data_32 = 0;

	if (attr->dm_offset + attr->length > dm->length)
		return EINVAL;

	/* DM access address must be aligned to 4 bytes */
	if (dm_va & 0x3)
		return EINVAL;

	if (attr->memcpy_dir == IBV_EXP_DM_CPY_TO_DEVICE) {
		while (copy_size >= sizeof(uint32_t)) {
			*((volatile uint32_t *)(uintptr_t)(dm_va + offset)) =
					*((uint32_t *)attr->host_addr + offset);
			offset += sizeof(uint32_t);
			copy_size -= sizeof(uint32_t);
		}

		if (copy_size) {
			memcpy(&data_32, attr->host_addr + offset, copy_size);
			*((volatile uint32_t *)(uintptr_t)(dm_va + offset)) =
					data_32;
		}
	} else {
		memcpy(attr->host_addr, (void *)(uintptr_t)dm_va, attr->length);
	}

	wc_wmb();

	return 0;
}

static struct ibv_cq *create_cq(struct ibv_context *context,
				int cqe,
				struct ibv_comp_channel *channel,
				int comp_vector,
				struct ibv_exp_cq_init_attr *attr)
{
	struct mlx5_create_cq		cmd;
	struct mlx5_exp_create_cq	cmd_e;
	struct mlx5_create_cq_resp	resp;
	struct mlx5_cq		       *cq;
	struct mlx5_context		*mctx = to_mctx(context);
	int				cqe_sz;
	int				ret;
	int				ncqe;
	int				thread_safe;
#ifdef MLX5_DEBUG
	FILE *fp = mctx->dbg_fp;
#endif

	if (!cqe) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "\n");
		errno = EINVAL;
		return NULL;
	}

	cq =  calloc(1, sizeof *cq);
	if (!cq) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "\n");
		return NULL;
	}

	memset(&cmd, 0, sizeof(cmd));
	memset(&cmd_e, 0, sizeof(cmd_e));
	cq->cons_index = 0;
	/* wait_index should start at value before 0 */
	cq->wait_index = (uint32_t)(-1);
	cq->wait_count = 0;

	cq->pattern = MLX5_CQ_PATTERN;
	thread_safe = !mlx5_single_threaded;
	if (attr && (attr->comp_mask & IBV_EXP_CQ_INIT_ATTR_RES_DOMAIN)) {
		if (!attr->res_domain) {
			errno = EINVAL;
			goto err;
		}
		thread_safe = (to_mres_domain(attr->res_domain)->attr.thread_model == IBV_EXP_THREAD_SAFE);
	}
	if (mlx5_lock_init(&cq->lock, thread_safe, mlx5_get_locktype()))
		goto err;

	cq->model_flags = thread_safe ? MLX5_CQ_MODEL_FLAG_THREAD_SAFE : 0;

	/* The additional entry is required for resize CQ */
	if (cqe <= 0) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "\n");
		errno = EINVAL;
		goto err_spl;
	}

	ncqe = align_queue_size(cqe + 1);
	if ((ncqe > (1 << 24)) || (ncqe < (cqe + 1))) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "ncqe %d\n", ncqe);
		errno = EINVAL;
		goto err_spl;
	}

	cqe_sz = get_cqe_size(context);
	if (cqe_sz < 0) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "\n");
		errno = -cqe_sz;
		goto err_spl;
	}

	if (attr && (attr->comp_mask & IBV_EXP_CQ_INIT_ATTR_PEER_DIRECT)) {
		if (check_peer_direct(mctx, attr->peer_direct_attrs,
				      MLX5_DBG_CQ,
				      IBV_EXP_PEER_OP_STORE_DWORD_CAP |
				      IBV_EXP_PEER_OP_POLL_AND_DWORD_CAP) ||
		    !(attr->peer_direct_attrs->caps &
				     (IBV_EXP_PEER_OP_POLL_NOR_DWORD_CAP |
				      IBV_EXP_PEER_OP_POLL_GEQ_DWORD_CAP))) {
			goto err_spl;
		}
		cq->peer_enabled = 1;
		cq->peer_ctx = attr->peer_direct_attrs;
	}

	if (mlx5_alloc_cq_buf(mctx, cq, &cq->buf_a, ncqe, cqe_sz)) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "\n");
		goto err_spl;
	}

	if (cq->peer_enabled &&
	    mlx5_alloc_cq_peer_buf(mctx, cq, ncqe)) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "\n");
		goto err_buf;
	}

	cq->dbrec  = mlx5_alloc_dbrec(mctx);
	if (!cq->dbrec) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "\n");
		goto err_peer_buf;
	}

	cq->dbrec[MLX5_CQ_SET_CI]	= 0;
	cq->dbrec[MLX5_CQ_ARM_DB]	= 0;
	cq->arm_sn			= 0;
	cq->cqe_sz			= cqe_sz;

	if (attr->comp_mask || mctx->cqe_comp_max_num) {
		if (attr->comp_mask & IBV_EXP_CQ_INIT_ATTR_FLAGS &&
		    attr->flags & ~EXP_CREATE_CQ_SUPPORTED_FLAGS) {
			mlx5_dbg(fp, MLX5_DBG_CQ,
				 "Unsupported creation flags requested\n");
			errno = EINVAL;
			goto err_db;
		}

		if (attr->comp_mask & IBV_EXP_CQ_INIT_ATTR_FLAGS &&
		    attr->flags & IBV_EXP_CQ_COMPRESSED_CQE) {
			attr->flags &= (~IBV_EXP_CQ_COMPRESSED_CQE);
			cq->creation_flags |=
				MLX5_CQ_CREATION_FLAG_COMPRESSED_CQE;
		}

		cmd_e.buf_addr = (uintptr_t) cq->buf_a.buf;
		cmd_e.db_addr  = (uintptr_t) cq->dbrec;
		cmd_e.cqe_size = cqe_sz;
		cmd_e.size_of_prefix = offsetof(struct mlx5_exp_create_cq,
						prefix_reserved);
		cmd_e.exp_data.comp_mask = MLX5_EXP_CREATE_CQ_MASK_CQE_COMP_EN |
					   MLX5_EXP_CREATE_CQ_MASK_CQE_COMP_RECV_TYPE;
		if (mctx->cqe_comp_max_num) {
			cmd_e.exp_data.cqe_comp_en = (mctx->enable_cqe_comp ||
						      (cq->creation_flags &
						       MLX5_CQ_CREATION_FLAG_COMPRESSED_CQE)) ? 1 : 0;
			cmd_e.exp_data.cqe_comp_recv_type = MLX5_CQE_FORMAT_HASH;
		}
	} else {
		cmd.buf_addr = (uintptr_t) cq->buf_a.buf;
		cmd.db_addr  = (uintptr_t) cq->dbrec;
		cmd.cqe_size = cqe_sz;
	}

	if (attr->comp_mask || cmd_e.exp_data.comp_mask)
		ret = ibv_exp_cmd_create_cq(context, ncqe - 1, channel,
					    comp_vector, &cq->ibv_cq,
					    &cmd_e.ibv_cmd,
					    sizeof(cmd_e.ibv_cmd),
					    sizeof(cmd_e) - sizeof(cmd_e.ibv_cmd),
					    &resp.ibv_resp, sizeof(resp.ibv_resp),
					    sizeof(resp) - sizeof(resp.ibv_resp), attr);
	else
		ret = ibv_cmd_create_cq(context, ncqe - 1, channel, comp_vector,
					&cq->ibv_cq, &cmd.ibv_cmd, sizeof cmd,
					&resp.ibv_resp, sizeof(resp));

	if (ret) {
		mlx5_dbg(fp, MLX5_DBG_CQ, "ret %d\n", ret);
		goto err_db;
	}

	if (attr->comp_mask & IBV_EXP_CQ_INIT_ATTR_FLAGS &&
	    attr->flags & IBV_EXP_CQ_TIMESTAMP)
		cq->creation_flags |=
			MLX5_CQ_CREATION_FLAG_COMPLETION_TIMESTAMP;

	cq->active_buf = &cq->buf_a;
	cq->resize_buf = NULL;
	cq->cqn = resp.cqn;
	cq->stall_enable = mctx->stall_enable;
	cq->stall_adaptive_enable = mctx->stall_adaptive_enable;
	cq->stall_cycles = mctx->stall_cycles;
	cq->cq_log_size = mlx5_ilog2(ncqe);

	return &cq->ibv_cq;

err_db:
	mlx5_free_db(mctx, cq->dbrec);

err_peer_buf:
	if (cq->peer_enabled)
		mlx5_free_actual_buf(mctx, &cq->peer_buf);

err_buf:
	mlx5_free_actual_buf(mctx, &cq->buf_a);

err_spl:
	mlx5_lock_destroy(&cq->lock);

err:
	free(cq);

	return NULL;
}

struct ibv_cq *mlx5_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel,
			      int comp_vector)
{
	struct ibv_exp_cq_init_attr attr;

	read_init_vars(to_mctx(context));
	memset(&attr, 0, sizeof(attr));

	return create_cq(context, cqe, channel, comp_vector, &attr);
}

struct ibv_cq *mlx5_create_cq_ex(struct ibv_context *context,
				 int cqe,
				 struct ibv_comp_channel *channel,
				 int comp_vector,
				 struct ibv_exp_cq_init_attr *attr)
{
	return create_cq(context, cqe, channel, comp_vector, attr);
}

int mlx5_resize_cq(struct ibv_cq *ibcq, int cqe)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_resize_cq_resp resp;
	struct mlx5_resize_cq cmd;
	struct mlx5_context *mctx = to_mctx(ibcq->context);
	int err;

	if (cqe < 0) {
		errno = EINVAL;
		return errno;
	}

	if (cq->peer_enabled) {
		mlx5_dbg(mctx->dbg_fp, MLX5_DBG_QP,
			 "CQ resize not supported for peer direct\n");
		return EINVAL;
	}

	memset(&cmd, 0, sizeof(cmd));
	memset(&resp, 0, sizeof(resp));

	if (((long long)cqe * 64) > INT_MAX)
		return EINVAL;

	mlx5_lock(&cq->lock);
	cq->active_cqes = cq->ibv_cq.cqe;
	if (cq->active_buf == &cq->buf_a)
		cq->resize_buf = &cq->buf_b;
	else
		cq->resize_buf = &cq->buf_a;

	cqe = align_queue_size(cqe + 1);
	if (cqe == ibcq->cqe + 1) {
		cq->resize_buf = NULL;
		err = 0;
		goto out;
	}

	/* currently we don't change cqe size */
	cq->resize_cqe_sz = cq->cqe_sz;
	cq->resize_cqes = cqe;
	err = mlx5_alloc_cq_buf(mctx, cq, cq->resize_buf, cq->resize_cqes, cq->resize_cqe_sz);
	if (err) {
		cq->resize_buf = NULL;
		errno = ENOMEM;
		goto out;
	}

	cmd.buf_addr = (uintptr_t)cq->resize_buf->buf;
	cmd.cqe_size = cq->resize_cqe_sz;

	err = ibv_cmd_resize_cq(ibcq, cqe - 1, &cmd.ibv_cmd, sizeof(cmd),
				&resp.ibv_resp, sizeof(resp));
	if (err)
		goto out_buf;

	mlx5_cq_resize_copy_cqes(cq);
	mlx5_free_actual_buf(mctx, cq->active_buf);
	cq->active_buf = cq->resize_buf;
	cq->ibv_cq.cqe = cqe - 1;
	cq->cq_log_size = mlx5_ilog2(cqe);
	mlx5_update_cons_index(cq);
	mlx5_unlock(&cq->lock);
	cq->resize_buf = NULL;
	return 0;

out_buf:
	mlx5_free_actual_buf(mctx, cq->resize_buf);
	cq->resize_buf = NULL;

out:
	mlx5_unlock(&cq->lock);
	return err;
}

int mlx5_destroy_cq(struct ibv_cq *ibcq)
{
	int ret;
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_context *ctx = to_mctx(ibcq->context);

	ret = ibv_cmd_destroy_cq(ibcq);
	if (ret)
		return ret;

	mlx5_free_db(ctx, cq->dbrec);
	mlx5_free_actual_buf(ctx, cq->active_buf);
	if (cq->peer_enabled)
		mlx5_free_actual_buf(ctx, &cq->peer_buf);
	free(cq);

	return 0;
}

struct ibv_srq *mlx5_create_srq(struct ibv_pd *pd,
				struct ibv_srq_init_attr *attr)
{
	struct mlx5_create_srq      cmd;
	struct mlx5_create_srq_resp resp;
	struct mlx5_srq		   *srq;
	int			    ret;
	struct mlx5_context	   *ctx;
	struct ibv_srq		   *ibsrq;

	ctx = to_mctx(pd->context);
	srq = mlx5_alloc_srq(pd->context, &attr->attr);
	if (!srq) {
		fprintf(stderr, "%s-%d:\n", __func__, __LINE__);
		return NULL;
	}
	ibsrq = (struct ibv_srq *)&srq->vsrq;
	srq->is_xsrq = 0;

	memset(&cmd, 0, sizeof cmd);
	cmd.buf_addr = (uintptr_t) srq->buf.buf;
	cmd.db_addr  = (uintptr_t) srq->db;
	if (srq->wq_sig)
		cmd.flags = MLX5_SRQ_FLAG_SIGNATURE;

	pthread_mutex_lock(&ctx->srq_table_mutex);
	ret = ibv_cmd_create_srq(pd, ibsrq, attr, &cmd.ibv_cmd, sizeof(cmd),
				 &resp.ibv_resp, sizeof(resp));
	if (ret)
		goto err_db;

	ret = mlx5_store_srq(ctx, resp.srqn, srq);
	if (ret)
		goto err_destroy;

	pthread_mutex_unlock(&ctx->srq_table_mutex);

	srq->srqn = resp.srqn;
	srq->rsc.rsn = resp.srqn;
	srq->rsc.type = MLX5_RSC_TYPE_SRQ;

	return ibsrq;

err_destroy:
	ibv_cmd_destroy_srq(ibsrq);

err_db:
	pthread_mutex_unlock(&ctx->srq_table_mutex);
	mlx5_free_srq(pd->context, srq);

	return NULL;
}

int mlx5_modify_srq(struct ibv_srq *srq,
		    struct ibv_srq_attr *attr,
		    int attr_mask)
{
	struct ibv_modify_srq cmd;

	if (srq->handle == LEGACY_XRC_SRQ_HANDLE)
		srq = (struct ibv_srq *)(((struct ibv_srq_legacy *) srq)->ibv_srq);

	return ibv_cmd_modify_srq(srq, attr, attr_mask, &cmd, sizeof cmd);
}

int mlx5_query_srq(struct ibv_srq *srq,
		    struct ibv_srq_attr *attr)
{
	struct ibv_query_srq cmd;
	if (srq->handle == LEGACY_XRC_SRQ_HANDLE)
		srq = (struct ibv_srq *)(((struct ibv_srq_legacy *) srq)->ibv_srq);

	return ibv_cmd_query_srq(srq, attr, &cmd, sizeof cmd);
}

int mlx5_destroy_srq(struct ibv_srq *srq)
{
	struct ibv_srq *legacy_srq = NULL;
	struct mlx5_srq *msrq;
	struct mlx5_context *ctx = to_mctx(srq->context);
	int ret;

	if (srq->handle == LEGACY_XRC_SRQ_HANDLE) {
		legacy_srq = srq;
		srq = (struct ibv_srq *)(((struct ibv_srq_legacy *) srq)->ibv_srq);
	}

	msrq = to_msrq(srq);
	if (msrq->cmd_qp) {
		ret = mlx5_destroy_qp(&msrq->cmd_qp->verbs_qp.qp);
		if (ret)
			return ret;
	}

	ret = ibv_cmd_destroy_srq(srq);
	if (ret)
		return ret;

	if (ctx->cqe_version && msrq->is_xsrq)
		mlx5_clear_uidx(ctx, msrq->rsc.rsn);
	else
		mlx5_clear_srq(ctx, msrq->srqn);

	free(msrq->tm_list);
	free(msrq->op);
	mlx5_free_srq(srq->context, msrq);

	if (legacy_srq)
		free(legacy_srq);

	return 0;
}

static int sq_overhead(struct ibv_exp_qp_init_attr *attr, struct mlx5_qp *qp,
		       int *inl_atom)
{
	int size1 = 0;
	int size2 = 0;
	int atom = 0;

	size_t mw_bind_size = sizeof(struct mlx5_wqe_umr_ctrl_seg) +
			      sizeof(struct mlx5_wqe_mkey_context_seg) +
			      max(sizeof(struct mlx5_wqe_umr_klm_seg), 64);

	switch (attr->qp_type) {
	case IBV_QPT_RC:
		size1 = sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_umr_ctrl_seg) +
			sizeof(struct mlx5_mkey_seg) +
			sizeof(struct mlx5_seg_repeat_block);
		size2 = sizeof(struct mlx5_wqe_ctrl_seg) + max(sizeof(struct mlx5_wqe_raddr_seg),
			    mw_bind_size);

		if (qp->enable_atomics) {
			if ((attr->comp_mask & IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG) &&
			    (attr->max_atomic_arg > 4))
				atom = 4 * attr->max_atomic_arg;
			/* TBD: change when we support data pointer args */
			if (inl_atom)
				*inl_atom = max(sizeof(struct mlx5_wqe_atomic_seg), atom);
		}

		break;

	case IBV_QPT_UC:
		size2 = sizeof(struct mlx5_wqe_ctrl_seg) +
			max(sizeof(struct mlx5_wqe_raddr_seg),
			    mw_bind_size);
		break;

	case IBV_QPT_UD:
		size1 = sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_umr_ctrl_seg) +
			sizeof(struct mlx5_mkey_seg) +
			sizeof(struct mlx5_seg_repeat_block);

		size2 = sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_datagram_seg);

		if (qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY)
			size2 += (sizeof(struct mlx5_wqe_eth_seg) + sizeof(struct mlx5_wqe_eth_pad));

		break;

	case IBV_QPT_XRC:
	case IBV_QPT_XRC_SEND:
		size2 = sizeof(struct mlx5_wqe_ctrl_seg) + mw_bind_size;
	case IBV_QPT_XRC_RECV:
		size2 = max(size2, sizeof(struct mlx5_wqe_ctrl_seg) +
			   sizeof(struct mlx5_wqe_xrc_seg) +
			   sizeof(struct mlx5_wqe_raddr_seg));

		if (qp->enable_atomics) {
			if ((attr->comp_mask & IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG) &&
			    (attr->max_atomic_arg > 4))
				atom = 4 * attr->max_atomic_arg;
			/* TBD: change when we support data pointer args */
			if (inl_atom)
				*inl_atom = max(sizeof(struct mlx5_wqe_atomic_seg), atom);
		}

		break;

	case IBV_EXP_QPT_DC_INI:
		size1 = sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_umr_ctrl_seg) +
			sizeof(struct mlx5_mkey_seg) +
			sizeof(struct mlx5_seg_repeat_block);

		size2 = sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_datagram_seg) +
			sizeof(struct mlx5_wqe_raddr_seg);
		if (qp->enable_atomics) {
			if ((attr->comp_mask & IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG) &&
			    (attr->max_atomic_arg > 4))
				atom = 4 * attr->max_atomic_arg;
			/* TBD: change when we support data pointer args */
			if (inl_atom)
				*inl_atom = max(sizeof(struct mlx5_wqe_atomic_seg), atom);
		}
		break;

	case IBV_QPT_RAW_ETH:
		size2 = sizeof(struct mlx5_wqe_ctrl_seg) +
			sizeof(struct mlx5_wqe_eth_seg);
		break;

	default:
		return -EINVAL;
	}

	if (qp->umr_en)
		return max(size1, size2);
	else
		return size2;
}

static int mlx5_max4(int t1, int t2, int t3, int t4)
{
	if (t1 < t2)
		t1 = t2;

	if (t1 < t3)
		t1 = t3;

	if (t1 < t4)
		return t4;

	return t1;
}

static int mlx5_calc_send_wqe(struct mlx5_context *ctx,
			      struct ibv_exp_qp_init_attr *attr,
			      struct mlx5_qp *qp)
{
	int inl_size = 0;
	int max_gather;
	int tot_size;
	int overhead;
	int inl_umr = 0;
	int inl_atom = 0;
	int t1 = 0;
	int t2 = 0;
	int t3 = 0;
	int t4 = 0;

	overhead = sq_overhead(attr, qp, &inl_atom);
	if (overhead < 0)
		return overhead;

	if ((attr->comp_mask & IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG))
		qp->max_atomic_arg = attr->max_atomic_arg;
	if ((attr->comp_mask & IBV_EXP_QP_INIT_ATTR_MAX_INL_KLMS) &&
	    attr->max_inl_send_klms)
		inl_umr = attr->max_inl_send_klms * 16;

	if (attr->cap.max_inline_data) {
		inl_size = align(sizeof(struct mlx5_wqe_inl_data_seg) +
				 attr->cap.max_inline_data, 16);
	}

	if (attr->comp_mask & IBV_EXP_QP_INIT_ATTR_MAX_TSO_HEADER) {
		overhead += align(attr->max_tso_header, 16);
		qp->max_tso_header = attr->max_tso_header;
	}

	max_gather = (ctx->max_sq_desc_sz -  overhead) /
		sizeof(struct mlx5_wqe_data_seg);
	if (attr->cap.max_send_sge > max_gather)
		return -EINVAL;

	if (inl_atom)
		t1 = overhead + sizeof(struct mlx5_wqe_data_seg) + inl_atom;

	t2 = overhead + attr->cap.max_send_sge * sizeof(struct mlx5_wqe_data_seg);

	t3 = overhead + inl_umr;
	t4 = overhead + inl_size;

	tot_size = mlx5_max4(t1, t2, t3, t4);

	if (tot_size > ctx->max_sq_desc_sz)
		return -EINVAL;

	return align(tot_size, MLX5_SEND_WQE_BB);
}

static int mlx5_calc_rcv_wqe(struct mlx5_context *ctx,
			     struct ibv_exp_qp_init_attr *attr,
			     struct mlx5_qp *qp)
{
	int size;
	int num_scatter;

	if (attr->srq)
		return 0;

	num_scatter = max(attr->cap.max_recv_sge, 1);
	size = sizeof(struct mlx5_wqe_data_seg) * num_scatter;
	if (qp->ctrl_seg.wq_sig)
		size += sizeof(struct mlx5_rwqe_sig);

	if (size < 0 || size > ctx->max_rq_desc_sz)
		return -EINVAL;

	size = mlx5_round_up_power_of_two(size);

	return size;
}

static int get_send_sge(struct ibv_exp_qp_init_attr *attr, int wqe_size, struct mlx5_qp *qp)
{
	int max_sge;
	int overhead = sq_overhead(attr, qp, NULL);

	if (attr->qp_type == IBV_QPT_RC)
		max_sge = (min(wqe_size, 512) -
			   sizeof(struct mlx5_wqe_ctrl_seg) -
			   sizeof(struct mlx5_wqe_raddr_seg)) /
		sizeof(struct mlx5_wqe_data_seg);
	else if (attr->qp_type == IBV_EXP_QPT_DC_INI)
		max_sge = (min(wqe_size, 512) -
			   sizeof(struct mlx5_wqe_ctrl_seg) -
			   sizeof(struct mlx5_wqe_datagram_seg) -
			   sizeof(struct mlx5_wqe_raddr_seg)) /
		sizeof(struct mlx5_wqe_data_seg);
	else if (attr->qp_type == IBV_QPT_XRC)
		max_sge = (min(wqe_size, 512) -
			   sizeof(struct mlx5_wqe_ctrl_seg) -
			   sizeof(struct mlx5_wqe_xrc_seg) -
			   sizeof(struct mlx5_wqe_raddr_seg)) /
		sizeof(struct mlx5_wqe_data_seg);
	else
		max_sge = (wqe_size - overhead) /
		sizeof(struct mlx5_wqe_data_seg);

	return min(max_sge, (wqe_size - overhead) /
		   sizeof(struct mlx5_wqe_data_seg));
}

static int mlx5_calc_sq_size(struct mlx5_context *ctx,
			     struct ibv_exp_qp_init_attr *attr,
			     struct mlx5_qp *qp)
{
	int wqe_size;
	int wq_size;
#ifdef MLX5_DEBUG
	FILE *fp = ctx->dbg_fp;
#endif

	if (!attr->cap.max_send_wr)
		return 0;

	wqe_size = mlx5_calc_send_wqe(ctx, attr, qp);
	if (wqe_size < 0) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		return wqe_size;
	}

	if (attr->qp_type == IBV_EXP_QPT_DC_INI &&
	    wqe_size > ctx->max_desc_sz_sq_dc) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		return -EINVAL;
	} else if (wqe_size > ctx->max_sq_desc_sz) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		return -EINVAL;
	}

	qp->data_seg.max_inline_data = wqe_size - sq_overhead(attr, qp, NULL) -
		sizeof(struct mlx5_wqe_inl_data_seg);
	attr->cap.max_inline_data = qp->data_seg.max_inline_data;

	/*
	 * to avoid overflow, we limit max_send_wr so
	 * that the multiplication will fit in int
	 */
	if (attr->cap.max_send_wr > 0x7fffffff / ctx->max_sq_desc_sz) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		return -ENOMEM;
	}

	wq_size = mlx5_round_up_power_of_two(attr->cap.max_send_wr * wqe_size);
	qp->sq.wqe_cnt = wq_size / MLX5_SEND_WQE_BB;
	if (qp->sq.wqe_cnt > ctx->max_send_wqebb) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		return -ENOMEM;
	}

	qp->sq.wqe_shift = mlx5_ilog2(MLX5_SEND_WQE_BB);
	qp->sq.max_gs = get_send_sge(attr, wqe_size, qp);
	if (qp->sq.max_gs < attr->cap.max_send_sge)
		return -ENOMEM;

	attr->cap.max_send_sge = qp->sq.max_gs;
	if (qp->umr_en) {
		qp->max_inl_send_klms = ((attr->qp_type == IBV_QPT_RC) ||
					    (attr->qp_type == IBV_EXP_QPT_DC_INI)) ?
					    attr->max_inl_send_klms : 0;
		attr->max_inl_send_klms = qp->max_inl_send_klms;
	}
	qp->sq.max_post = wq_size / wqe_size;

	return wq_size;
}

static int qpt_has_rq(enum ibv_qp_type qpt)
{
	switch (qpt) {
	case IBV_QPT_RC:
	case IBV_QPT_UC:
	case IBV_QPT_UD:
	case IBV_QPT_RAW_ETH:
		return 1;

	case IBV_QPT_XRC:
	case IBV_QPT_XRC_SEND:
	case IBV_QPT_XRC_RECV:
	case IBV_EXP_QPT_DC_INI:
		return 0;
	}
	return 0;
}

static int mlx5_calc_rwq_size(struct mlx5_context *ctx,
			      struct mlx5_rwq *rwq,
			      struct ibv_exp_wq_init_attr *attr)
{
	int wqe_size;
	int wq_size;
	int num_scatter;
	int scat_spc;
	int mp_rq = !!(attr->comp_mask & IBV_EXP_CREATE_WQ_MP_RQ);

	if (!attr->max_recv_wr)
		return -EINVAL;

	/* TBD: check caps for RQ */
	num_scatter = max(attr->max_recv_sge, 1);
	wqe_size = sizeof(struct mlx5_wqe_data_seg) * num_scatter +
		   /* In case of mp_rq the WQE format is like SRQ.
		    * Need to add the extra octword even when we don't
		    * use linked list.
		    */
		   (mp_rq ? sizeof(struct mlx5_wqe_srq_next_seg) : 0);

	if (rwq->wq_sig)
		wqe_size += sizeof(struct mlx5_rwqe_sig);

	if (wqe_size <= 0 || wqe_size > ctx->max_rq_desc_sz)
		return -EINVAL;

	wqe_size = mlx5_round_up_power_of_two(wqe_size);
	wq_size = mlx5_round_up_power_of_two(attr->max_recv_wr) * wqe_size;
	wq_size = max(wq_size, MLX5_SEND_WQE_BB);
	rwq->rq.wqe_cnt = wq_size / wqe_size;
	rwq->rq.wqe_shift = mlx5_ilog2(wqe_size);
	rwq->rq.max_post = 1 << mlx5_ilog2(wq_size / wqe_size);
	scat_spc = wqe_size -
		   ((rwq->wq_sig) ? sizeof(struct mlx5_rwqe_sig) : 0) -
		   (mp_rq ? sizeof(struct mlx5_wqe_srq_next_seg) : 0);
	rwq->rq.max_gs = scat_spc / sizeof(struct mlx5_wqe_data_seg);
	return wq_size;
}

static int mlx5_calc_rq_size(struct mlx5_context *ctx,
			     struct ibv_exp_qp_init_attr *attr,
			     struct mlx5_qp *qp)
{
	int wqe_size;
	int wq_size;
	int scat_spc;
#ifdef MLX5_DEBUG
	FILE *fp = ctx->dbg_fp;
#endif

	if (!attr->cap.max_recv_wr || !qpt_has_rq(attr->qp_type))
		return 0;

	if (attr->cap.max_recv_wr > ctx->max_recv_wr) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		return -EINVAL;
	}

	wqe_size = mlx5_calc_rcv_wqe(ctx, attr, qp);
	if (wqe_size < 0 || wqe_size > ctx->max_rq_desc_sz) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		return -EINVAL;
	}

	wq_size = mlx5_round_up_power_of_two(attr->cap.max_recv_wr) * wqe_size;
	if (wqe_size) {
		wq_size = max(wq_size, MLX5_SEND_WQE_BB);
		qp->rq.wqe_cnt = wq_size / wqe_size;
		qp->rq.wqe_shift = mlx5_ilog2(wqe_size);
		qp->rq.max_post = 1 << mlx5_ilog2(wq_size / wqe_size);
		scat_spc = wqe_size -
			((qp->ctrl_seg.wq_sig) ? sizeof(struct mlx5_rwqe_sig) : 0);
		qp->rq.max_gs = scat_spc / sizeof(struct mlx5_wqe_data_seg);
	} else {
		qp->rq.wqe_cnt = 0;
		qp->rq.wqe_shift = 0;
		qp->rq.max_post = 0;
		qp->rq.max_gs = 0;
	}
	return wq_size;
}

static int mlx5_calc_wq_size(struct mlx5_context *ctx,
			     struct ibv_exp_qp_init_attr *attr,
			     struct mlx5_qp *qp)
{
	int ret;
	int result;

	ret = mlx5_calc_sq_size(ctx, attr, qp);
	if (ret < 0)
		return ret;

	result = ret;
	ret = mlx5_calc_rq_size(ctx, attr, qp);
	if (ret < 0)
		return ret;

	result += ret;

	qp->sq.offset = ret;
	qp->rq.offset = 0;

	return result;
}

static void map_uuar(struct ibv_context *context, struct mlx5_qp *qp,
		     int uuar_index)
{
	struct mlx5_context *ctx = to_mctx(context);

	qp->gen_data.bf = &ctx->bfs[uuar_index];
}

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

static void mlx5_free_rwq_buf(struct mlx5_rwq *rwq, struct ibv_context *context)
{
	struct mlx5_context *ctx = to_mctx(context);

	mlx5_free_actual_buf(ctx, &rwq->buf);
	if (rwq->consumed_strides_counter)
		free(rwq->consumed_strides_counter);

	free(rwq->rq.wrid);
}

static int mlx5_alloc_rwq_buf(struct ibv_context *context,
			      struct mlx5_rwq *rwq,
			      int size,
			      enum mlx5_rsc_type rsc_type)
{
	int err;
	enum mlx5_alloc_type default_alloc_type = MLX5_ALLOC_TYPE_PREFER_CONTIG;

	rwq->rq.wrid = malloc(rwq->rq.wqe_cnt * sizeof(uint64_t));
	if (!rwq->rq.wrid) {
		errno = ENOMEM;
		return -1;
	}

	if (rsc_type == MLX5_RSC_TYPE_MP_RWQ) {
		rwq->consumed_strides_counter = calloc(1, rwq->rq.wqe_cnt * sizeof(uint32_t));
		if (!rwq->consumed_strides_counter) {
			errno = ENOMEM;
			goto free_wr_id;
		}
	}

	rwq->buf.numa_req.valid = 1;
	rwq->buf.numa_req.numa_id = to_mctx(context)->numa_id;
	err = mlx5_alloc_preferred_buf(to_mctx(context), &rwq->buf,
				      align(rwq->buf_size, to_mdev
				      (context->device)->page_size),
				      to_mdev(context->device)->page_size,
				      default_alloc_type,
				      MLX5_RWQ_PREFIX);

	if (err) {
		errno = ENOMEM;
		goto free_strd_cnt;
	}

	return 0;

free_strd_cnt:
	if (rwq->consumed_strides_counter)
		free(rwq->consumed_strides_counter);

free_wr_id:
	free(rwq->rq.wrid);

	return -1;
}
static int mlx5_alloc_qp_buf(struct ibv_context *context,
			     struct ibv_exp_qp_init_attr *attr,
			     struct mlx5_qp *qp,
			     int size)
{
	int err;
	enum mlx5_alloc_type alloc_type;
	enum mlx5_alloc_type default_alloc_type = MLX5_ALLOC_TYPE_PREFER_CONTIG;
	const char *qp_huge_key;
	struct mlx5_context *ctx = to_mctx(context);
	struct mlx5_device *dev = to_mdev(ctx->ibv_ctx.device);

	if (qp->sq.wqe_cnt) {
		qp->sq.wrid = malloc(qp->sq.wqe_cnt * sizeof(*qp->sq.wrid));
		if (!qp->sq.wrid) {
			errno = ENOMEM;
			err = -1;
			return err;
		}

		qp->sq.wr_data = malloc(qp->sq.wqe_cnt * sizeof(*qp->sq.wr_data));
		if (!qp->sq.wr_data) {
			errno = ENOMEM;
			err = -1;
			goto ex_wrid;
		}
	}

	qp->gen_data.wqe_head = malloc(qp->sq.wqe_cnt * sizeof(*qp->gen_data.wqe_head));
	if (!qp->gen_data.wqe_head) {
		errno = ENOMEM;
		err = -1;
			goto ex_wrid;
	}

	if (qp->rq.wqe_cnt) {
		qp->rq.wrid = malloc(qp->rq.wqe_cnt * sizeof(uint64_t));
		if (!qp->rq.wrid) {
			errno = ENOMEM;
			err = -1;
			goto ex_wrid;
		}
	}

	/* compatability support */
	qp_huge_key  = qptype2key(qp->verbs_qp.qp.qp_type);
	if (mlx5_use_huge(context, qp_huge_key))
		default_alloc_type = MLX5_ALLOC_TYPE_HUGE;

	mlx5_get_alloc_type(context, MLX5_QP_PREFIX, &alloc_type,
			    default_alloc_type);

	qp->buf.numa_req.valid = 1;
	qp->buf.numa_req.numa_id = to_mctx(context)->numa_id;
	err = mlx5_alloc_preferred_buf(ctx, &qp->buf,
				       align(qp->buf_size, dev->page_size),
				       dev->page_size,
				       alloc_type, MLX5_QP_PREFIX);

	if (err) {
		err = -ENOMEM;
		goto ex_wrid;
	}

	memset(qp->buf.buf, 0, qp->buf_size);

	if (attr->qp_type == IBV_QPT_RAW_ETH || qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) {
		/* For Raw Ethernet QP, allocate a separate buffer for the SQ, same for underlay QP */
		err = mlx5_alloc_preferred_buf(ctx, &qp->sq_buf,
					      align(qp->sq_buf_size,
						    dev->page_size),
					      dev->page_size,
					      alloc_type,
					      MLX5_QP_PREFIX);
		if (err) {
			err = -ENOMEM;
			goto rq_buf;
		}

		memset(qp->sq_buf.buf, 0, qp->buf_size - qp->sq.offset);
	}

	return 0;
rq_buf:
	mlx5_free_actual_buf(to_mctx(qp->verbs_qp.qp.context), &qp->buf);
ex_wrid:
	if (qp->rq.wrid)
		free(qp->rq.wrid);

	if (qp->gen_data.wqe_head)
		free(qp->gen_data.wqe_head);

	if (qp->sq.wr_data)
		free(qp->sq.wr_data);

	if (qp->sq.wrid)
		free(qp->sq.wrid);

	return err;
}

static void mlx5_free_qp_buf(struct mlx5_qp *qp)
{
	struct mlx5_context *ctx = to_mctx(qp->verbs_qp.qp.context);

	mlx5_free_actual_buf(ctx, &qp->buf);

	if (qp->sq_buf.buf)
		mlx5_free_actual_buf(ctx, &qp->sq_buf);

	if (qp->rq.wrid)
		free(qp->rq.wrid);

	if (qp->gen_data.wqe_head)
		free(qp->gen_data.wqe_head);

	if (qp->sq.wrid)
		free(qp->sq.wrid);

	if (qp->sq.wr_data)
		free(qp->sq.wr_data);
}

static void update_caps(struct ibv_context *context)
{
	struct mlx5_context *ctx;
	struct ibv_exp_device_attr attr;
	int err;

	ctx = to_mctx(context);
	if (ctx->info.valid)
		return;

	memset(&attr, 0, sizeof(attr));
	attr.comp_mask = IBV_EXP_DEVICE_ATTR_RESERVED - 1;
	if (ibv_exp_query_device(context, &attr)) {
		struct ibv_device_attr device_legacy_attr = {};

		err = ibv_query_device(context, &device_legacy_attr);
		if (err)
			return;

		memcpy(&attr, &device_legacy_attr, sizeof(device_legacy_attr));
	}

	ctx->info.exp_atomic_cap = attr.exp_atomic_cap;
	ctx->info.valid = 1;
	ctx->max_sge = attr.max_sge;
	if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_UMR)
		ctx->max_send_wqe_inline_klms =
			attr.umr_caps.max_send_wqe_inline_klms;
	if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_MASKED_ATOMICS) {
		ctx->info.bit_mask_log_atomic_arg_sizes =
			attr.masked_atomic.masked_log_atomic_arg_sizes;
		ctx->info.masked_log_atomic_arg_sizes_network_endianness =
			attr.masked_atomic.masked_log_atomic_arg_sizes_network_endianness;
	}
	if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_EXT_ATOMIC_ARGS)
		ctx->info.bit_mask_log_atomic_arg_sizes |=
			attr.ext_atom.log_atomic_arg_sizes;
	if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_TSO_CAPS)
		ctx->max_tso = attr.tso_caps.max_tso;

	return;
}

static inline int is_xrc_tgt(int type)
{
	return (type == IBV_QPT_XRC_RECV);
}

static struct ibv_qp *create_qp(struct ibv_context *context,
				struct ibv_exp_qp_init_attr *attrx,
				int is_exp)
{
	struct mlx5_create_qp		cmd;
	struct mlx5_create_qp_resp	resp;
	struct mlx5_exp_create_qp	cmdx;
	struct mlx5_exp_create_qp_resp	respx;
	struct mlx5_qp		       *qp;
	int				ret;
	struct mlx5_context	       *ctx = to_mctx(context);
	struct ibv_qp		       *ibqp;
	struct mlx5_drv_create_qp      *drv;
	struct mlx5_exp_drv_create_qp  *drvx;
	int				lib_cmd_size;
	int				drv_cmd_size;
	int				lib_resp_size;
	int				drv_resp_size;
	int				thread_safe = !mlx5_single_threaded;
	void			      *_cmd;
	void			      *_resp;
#ifdef MLX5_DEBUG
	FILE *fp = ctx->dbg_fp;
#endif

	/* Use experimental path when driver pass experimental data */
	is_exp = is_exp || (ctx->cqe_version != 0) ||
		 (attrx->qp_type == IBV_QPT_RAW_ETH);

	update_caps(context);
	qp = aligned_calloc(sizeof(*qp));
	if (!qp) {
		mlx5_dbg(fp, MLX5_DBG_QP, "failed to allocate mlx5 qp\n");
		return NULL;
	}
	ibqp = (struct ibv_qp *)&qp->verbs_qp;

	if (is_exp) {
		memset(&cmdx, 0, sizeof(cmdx));
		memset(&respx, 0, sizeof(respx));
		drv = (struct mlx5_drv_create_qp *)(void *)(&cmdx.drv);
		drvx = &cmdx.drv;
		drvx->size_of_prefix = offsetof(struct mlx5_exp_drv_create_qp, prefix_reserved);
		_cmd = &cmdx.ibv_cmd;
		_resp = &respx.ibv_resp;
		lib_cmd_size = sizeof(cmdx.ibv_cmd);
		drv_cmd_size = sizeof(*drvx);
		lib_resp_size = sizeof(respx.ibv_resp);
		drv_resp_size = sizeof(respx) - sizeof(respx.ibv_resp);
	} else {
		memset(&cmd, 0, sizeof(cmd));
		drv = &cmd.drv;
		_cmd = &cmd.ibv_cmd;
		_resp = &resp.ibv_resp;
		lib_cmd_size = sizeof(cmd.ibv_cmd);
		drv_cmd_size = sizeof(*drv);
		lib_resp_size = sizeof(resp.ibv_resp);
		drv_resp_size = sizeof(resp) - sizeof(resp.ibv_resp);
	}

	if (is_exp && attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_ASSOCIATED_QPN) {
		if (attrx->qp_type != IBV_QPT_UD) {
			errno = EINVAL;
			goto err;
		}

		qp->flags |= MLX5_QP_FLAGS_USE_UNDERLAY;
	}

	if ((attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_RX_HASH) && attrx->qp_type == IBV_QPT_RAW_ETH) {
		if (attrx->send_cq || attrx->recv_cq || attrx->srq ||
			attrx->cap.max_inline_data || attrx->cap.max_recv_sge ||
			attrx->cap.max_recv_wr || attrx->cap.max_send_sge ||
			 attrx->cap.max_send_wr) {
			errno = EINVAL;
			goto err;
		}

		ret = ibv_exp_cmd_create_qp(context, &qp->verbs_qp,
				    sizeof(qp->verbs_qp),
				    attrx,
				    _cmd,
				    lib_cmd_size,
				    0,
				    _resp,
				    lib_resp_size,
				    0, 1);
		if (ret)
			goto err;

		qp->rx_qp = 1;
		return ibqp;
	}

	qp->ctrl_seg.wq_sig = qp_sig_enabled(context);
	if (qp->ctrl_seg.wq_sig)
		drv->flags |= MLX5_QP_FLAG_SIGNATURE;

	if ((ctx->info.exp_atomic_cap == IBV_EXP_ATOMIC_HCA_REPLY_BE) &&
	    (attrx->exp_create_flags & IBV_EXP_QP_CREATE_ATOMIC_BE_REPLY)) {
			qp->enable_atomics = 1;
	} else if ((ctx->info.exp_atomic_cap == IBV_EXP_ATOMIC_HCA) ||
		   (ctx->info.exp_atomic_cap == IBV_EXP_ATOMIC_GLOB)) {
		qp->enable_atomics = 1;
	}

	if ((attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_MAX_INL_KLMS) &&
	    (!(attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS) ||
	     !(attrx->exp_create_flags & IBV_EXP_QP_CREATE_UMR))) {
		errno = EINVAL;
		goto err;
	}

	if ((attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS) &&
	    (attrx->exp_create_flags & IBV_EXP_QP_CREATE_UMR) &&
	    !(attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_MAX_INL_KLMS)) {
		errno = EINVAL;
		goto err;
	}

	if ((attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS) &&
	    (attrx->exp_create_flags & IBV_EXP_QP_CREATE_UMR))
		qp->umr_en = 1;

	if ((attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_MAX_TSO_HEADER) &&
	    (attrx->qp_type != IBV_QPT_RAW_PACKET)) {
		errno = EINVAL;
		goto err;
	}

	if (attrx->cap.max_send_sge > ctx->max_sge) {
		errno = EINVAL;
		goto err;
	}

	if ((attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS) &&
	    (attrx->exp_create_flags & IBV_EXP_QP_CREATE_EC_PARITY_EN)) {
		attrx->exp_create_flags |= IBV_EXP_QP_CREATE_CROSS_CHANNEL;
		attrx->cap.max_send_wr *= 2;
	}

	if (qp->umr_en && (attrx->max_inl_send_klms >
			   ctx->max_send_wqe_inline_klms)) {
		errno = EINVAL;
		goto err;
	}

	ret = mlx5_calc_wq_size(ctx, attrx, qp);
	if (ret < 0) {
		errno = -ret;
		goto err;
	}

	if (attrx->qp_type == IBV_QPT_RAW_ETH || qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) {
		qp->buf_size = qp->sq.offset;
		qp->sq_buf_size = ret - qp->buf_size;
	} else {
		qp->buf_size = ret;
		qp->sq_buf_size = 0;
	}

	if (attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_CREATE_FLAGS) {
		qp->gen_data.create_flags = attrx->exp_create_flags & IBV_EXP_QP_CREATE_MASK;
		if (qp->gen_data.create_flags & IBV_EXP_QP_CREATE_MANAGED_SEND)
			qp->gen_data.create_flags |= CREATE_FLAG_NO_DOORBELL;
	}

	if (attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_PEER_DIRECT) {
		if (check_peer_direct(ctx, attrx->peer_direct_attrs,
				      MLX5_DBG_QP,
				      IBV_EXP_PEER_OP_FENCE_CAP |
				      IBV_EXP_PEER_OP_STORE_DWORD_CAP |
				      IBV_EXP_PEER_OP_STORE_QWORD_CAP))
			goto err;
		qp->peer_enabled = 1;
		qp->peer_ctx = attrx->peer_direct_attrs;
		qp->gen_data.create_flags |= CREATE_FLAG_NO_DOORBELL;
	}

	if (mlx5_alloc_qp_buf(context, attrx, qp, ret)) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		goto err;
	}

	if (attrx->qp_type == IBV_QPT_RAW_ETH || qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) {
		qp->gen_data.sqstart = qp->sq_buf.buf;
		qp->gen_data.sqend = qp->sq_buf.buf +
				     (qp->sq.wqe_cnt << qp->sq.wqe_shift);
	} else {
		qp->gen_data.sqstart = qp->buf.buf + qp->sq.offset;
		qp->gen_data.sqend = qp->buf.buf + qp->sq.offset +
				     (qp->sq.wqe_cnt << qp->sq.wqe_shift);
	}
	qp->odp_data.pd = to_mpd(attrx->pd);

	mlx5_init_qp_indices(qp);

	/* Check if UAR provided by resource domain */
	if (attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_RES_DOMAIN) {
		struct mlx5_res_domain *res_domain = to_mres_domain(attrx->res_domain);

		drvx->exp.comp_mask |= MLX5_EXP_CREATE_QP_MASK_WC_UAR_IDX;
		if (res_domain->send_db) {
			drvx->exp.wc_uar_index = res_domain->send_db->wc_uar->uar_idx;
			qp->gen_data.bf = &res_domain->send_db->bf;
		} else {
			/* If we didn't allocate dedicated BF for this resource
			 * domain we'll ask the kernel to provide UUAR that uses
			 * DB only (no BF)
			 */
			drvx->exp.wc_uar_index = MLX5_EXP_CREATE_QP_DB_ONLY_UUAR;
		}
		thread_safe = (res_domain->attr.thread_model == IBV_EXP_THREAD_SAFE);
	}
	if (mlx5_lock_init(&qp->sq.lock, thread_safe, mlx5_get_locktype()) ||
	    mlx5_lock_init(&qp->rq.lock, thread_safe, mlx5_get_locktype()))
		goto err_free_qp_buf;
	qp->gen_data.model_flags = thread_safe ? MLX5_QP_MODEL_FLAG_THREAD_SAFE : 0;

	if (qp->peer_enabled && qp->peer_ctx->buf_alloc) {
		struct ibv_exp_peer_buf_alloc_attr attr;

		attr.length = ctx->cache_line_size;
		attr.peer_id = qp->peer_ctx->peer_id;
		attr.dir = IBV_EXP_PEER_DIRECTION_FROM_PEER |
			   IBV_EXP_PEER_DIRECTION_TO_HCA;
		attr.alignment = max(ctx->cache_line_size, MLX5_SEND_WQE_BB);
		qp->peer_db_buf = qp->peer_ctx->buf_alloc(&attr);
		if (qp->peer_db_buf)
			qp->gen_data.db = qp->peer_db_buf->addr;
	}

	if (!qp->gen_data.db)
		qp->gen_data.db = mlx5_alloc_dbrec(ctx);
	if (!qp->gen_data.db) {
		mlx5_dbg(fp, MLX5_DBG_QP, "\n");
		goto err_free_qp_buf;
	}

	qp->gen_data.db[MLX5_RCV_DBR] = 0;
	qp->gen_data.db[MLX5_SND_DBR] = 0;
	qp->rq.buff = qp->buf.buf + qp->rq.offset;
	qp->sq.buff = qp->buf.buf + qp->sq.offset;
	qp->rq.db = &qp->gen_data.db[MLX5_RCV_DBR];
	qp->sq.db = &qp->gen_data.db[MLX5_SND_DBR];

	drv->buf_addr = (uintptr_t) qp->buf.buf;
	if (attrx->qp_type == IBV_QPT_RAW_ETH || qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) {
		drvx->exp.sq_buf_addr = (uintptr_t)qp->sq_buf.buf;
		drvx->exp.flags |= MLX5_EXP_CREATE_QP_MULTI_PACKET_WQE_REQ_FLAG;
		drvx->exp.comp_mask |= MLX5_EXP_CREATE_QP_MASK_SQ_BUFF_ADD |
				       MLX5_EXP_CREATE_QP_MASK_FLAGS_IDX;
		if (qp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) {
			drvx->exp.comp_mask |= MLX5_EXP_CREATE_QP_MASK_ASSOC_QPN;
			drvx->exp.associated_qpn = attrx->associated_qpn;
		}
	}
	drv->db_addr  = (uintptr_t) qp->gen_data.db;
	drv->sq_wqe_count = qp->sq.wqe_cnt;
	drv->rq_wqe_count = qp->rq.wqe_cnt;
	drv->rq_wqe_shift = qp->rq.wqe_shift;

	if (qp->peer_enabled && qp->peer_ctx->register_va) {
		qp->peer_va_ids[MLX5_QP_PEER_VA_ID_DBR] =
			qp->peer_ctx->register_va((uint32_t *)qp->gen_data.db,
						  ctx->cache_line_size,
						  qp->peer_ctx->peer_id,
						  qp->peer_db_buf);
		if (!qp->peer_va_ids[MLX5_QP_PEER_VA_ID_DBR]) {
			mlx5_dbg(fp, MLX5_DBG_QP, "\n");
			goto err_rq_db;
		}
	}

	if (!ctx->cqe_version) {
		pthread_mutex_lock(&ctx->rsc_table_mutex);
	} else if (!is_xrc_tgt(attrx->qp_type)) {
		drvx->exp.uidx = mlx5_store_uidx(ctx, qp);
		if (drvx->exp.uidx < 0) {
			mlx5_dbg(fp, MLX5_DBG_QP, "Couldn't find free user index\n");
			goto err_peer_rq_db;
		}
		drvx->exp.comp_mask |= MLX5_EXP_CREATE_QP_MASK_UIDX;
	}

	ret = ibv_exp_cmd_create_qp(context, &qp->verbs_qp,
				    sizeof(qp->verbs_qp),
				    attrx,
				    _cmd,
				    lib_cmd_size,
				    drv_cmd_size,
				    _resp,
				    lib_resp_size,
				    drv_resp_size,
				    /* Force experimental */
				    is_exp);
	if (ret) {
		mlx5_dbg(fp, MLX5_DBG_QP, "ret %d\n", ret);
		goto err_free_uidx;
	}

	if (!ctx->cqe_version) {
		ret = mlx5_store_rsc(ctx, ibqp->qp_num, qp);
		if (ret) {
			mlx5_dbg(fp, MLX5_DBG_QP, "ret %d\n", ret);
			goto err_destroy;
		}
		pthread_mutex_unlock(&ctx->rsc_table_mutex);
	}

	/* Update related BF mapping when uuar not provided by resource domain */
	if (!(attrx->comp_mask & IBV_EXP_QP_INIT_ATTR_RES_DOMAIN) ||
	    !to_mres_domain(attrx->res_domain)->send_db) {
		if (is_exp)
			map_uuar(context, qp, respx.uuar_index);
		else
			map_uuar(context, qp, resp.uuar_index);
	}
	qp->gen_data_warm.pattern = MLX5_QP_PATTERN;

	if (qp->peer_enabled && qp->peer_ctx->register_va) {
		qp->peer_va_ids[MLX5_QP_PEER_VA_ID_BF] =
			qp->peer_ctx->register_va(qp->gen_data.bf->reg +
						  qp->gen_data.bf->offset,
						  qp->gen_data.bf->buf_size,
						  qp->peer_ctx->peer_id,
						  IBV_EXP_PEER_IOMEMORY);
		if (!qp->peer_va_ids[MLX5_QP_PEER_VA_ID_BF]) {
			mlx5_dbg(fp, MLX5_DBG_QP, "\n");
			goto err_destroy;
		}
	}

	qp->rq.max_post = qp->rq.wqe_cnt;
	if (attrx->sq_sig_all)
		qp->sq_signal_bits = MLX5_WQE_CTRL_CQ_UPDATE;
	else
		qp->sq_signal_bits = 0;

	attrx->cap.max_send_wr = qp->sq.max_post;
	attrx->cap.max_recv_wr = qp->rq.max_post;
	attrx->cap.max_recv_sge = qp->rq.max_gs;
	qp->rsc.type = MLX5_RSC_TYPE_QP;
	if (is_exp && (drvx->exp.comp_mask & MLX5_EXP_CREATE_QP_MASK_UIDX))
		qp->rsc.rsn = drvx->exp.uidx;
	else
		qp->rsc.rsn = ibqp->qp_num;

	if (is_exp && (respx.exp.comp_mask & MLX5_EXP_CREATE_QP_RESP_MASK_FLAGS_IDX) &&
	    (respx.exp.flags & MLX5_EXP_CREATE_QP_RESP_MULTI_PACKET_WQE_FLAG))
		qp->gen_data.model_flags |= MLX5_QP_MODEL_MULTI_PACKET_WQE;

	mlx5_build_ctrl_seg_data(qp, ibqp->qp_num);
	qp->gen_data_warm.qp_type = ibqp->qp_type;
	mlx5_update_post_send_one(qp, ibqp->state, ibqp->qp_type);

	return ibqp;

err_destroy:
	ibv_cmd_destroy_qp(ibqp);
err_free_uidx:
	if (!ctx->cqe_version)
		pthread_mutex_unlock(&to_mctx(context)->rsc_table_mutex);
	else if (!is_xrc_tgt(attrx->qp_type))
		mlx5_clear_uidx(ctx, drvx->exp.uidx);
err_peer_rq_db:
	if (qp->peer_enabled)
		free_peer_va(qp);

err_rq_db:
	if (qp->peer_db_buf)
		qp->peer_ctx->buf_release(qp->peer_db_buf);
	else
		mlx5_free_db(ctx, qp->gen_data.db);

err_free_qp_buf:
	mlx5_free_qp_buf(qp);
err:
	free(qp);

	return NULL;
}

struct ibv_qp *mlx5_drv_create_qp(struct ibv_context *context,
				  struct ibv_qp_init_attr_ex *attrx)
{
	if (attrx->comp_mask >= IBV_QP_INIT_ATTR_RESERVED) {
		errno = EINVAL;
		return NULL;
	}

	return create_qp(context, (struct ibv_exp_qp_init_attr *)attrx, 1);
}

struct ibv_qp *mlx5_exp_create_qp(struct ibv_context *context,
				  struct ibv_exp_qp_init_attr *attrx)
{
	return create_qp(context, attrx, 1);
}

struct ibv_qp *mlx5_create_qp(struct ibv_pd *pd,
			      struct ibv_qp_init_attr *attr)
{
	struct ibv_exp_qp_init_attr attrx;
	struct ibv_qp *qp;
	int copy_sz = offsetof(struct ibv_qp_init_attr, xrc_domain);

	memset(&attrx, 0, sizeof(attrx));
	memcpy(&attrx, attr, copy_sz);
	attrx.comp_mask = IBV_QP_INIT_ATTR_PD;
	attrx.pd = pd;
	qp = create_qp(pd->context, &attrx, 0);
	if (qp)
		memcpy(attr, &attrx, copy_sz);

	return qp;
}

struct ibv_exp_rwq_ind_table *mlx5_exp_create_rwq_ind_table(struct ibv_context *context,
							    struct ibv_exp_rwq_ind_table_init_attr *init_attr)
{
	struct ibv_exp_create_rwq_ind_table *cmd;
	struct mlx5_exp_create_rwq_ind_table_resp resp;
	struct ibv_exp_rwq_ind_table *ind_table;
	uint32_t required_tbl_size;
	int num_tbl_entries;
	int cmd_size;
	int err;

	num_tbl_entries = 1 << init_attr->log_ind_tbl_size;
	/* Data must be u64 aligned */
	required_tbl_size = (num_tbl_entries * sizeof(uint32_t)) < sizeof(uint64_t) ?
			sizeof(uint64_t) : (num_tbl_entries * sizeof(uint32_t));

	cmd_size = required_tbl_size + sizeof(*cmd);
	cmd = calloc(1, cmd_size);
	if (!cmd)
		return NULL;
	memset(&resp, 0, sizeof(resp));

	ind_table = calloc(1, sizeof(*ind_table));
	if (!ind_table)
		goto free_cmd;

	err = ibv_exp_cmd_create_rwq_ind_table(context, init_attr, ind_table, cmd,
					       cmd_size, cmd_size, &resp.ibv_resp, sizeof(resp.ibv_resp),
					       sizeof(resp));
	if (err)
		goto err;

	free(cmd);
	return ind_table;

err:
	free(ind_table);
free_cmd:
	free(cmd);
	return NULL;
}

int mlx5_exp_destroy_rwq_ind_table(struct ibv_exp_rwq_ind_table *rwq_ind_table)
{
	struct mlx5_exp_destroy_rwq_ind_table cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = ibv_exp_cmd_destroy_rwq_ind_table(rwq_ind_table);

	if (ret)
		return ret;

	free(rwq_ind_table);
	return 0;
}

struct ibv_exp_wq *mlx5_exp_create_wq(struct ibv_context *context,
				      struct ibv_exp_wq_init_attr *attr)
{
	struct mlx5_exp_create_wq		cmd;
	struct mlx5_exp_create_wq_resp	resp;
	int				err;
	struct mlx5_rwq 		*rwq;
	struct mlx5_context	*ctx = to_mctx(context);
	int ret;
	int thread_safe = !mlx5_single_threaded;
	struct ibv_exp_device_attr device_attr;
	enum mlx5_rsc_type	rsc_type;
#ifdef MLX5_DEBUG
	FILE *fp = ctx->dbg_fp;
#endif

	if (attr->wq_type != IBV_EXP_WQT_RQ)
		return NULL;

	memset(&cmd, 0, sizeof(cmd));
	memset(&resp, 0, sizeof(resp));

	rwq = aligned_calloc(sizeof(*rwq));
	if (!rwq)
		return NULL;

	rwq->wq_sig = rwq_sig_enabled(context);
	if (rwq->wq_sig)
		cmd.drv.flags = MLX5_RWQ_FLAG_SIGNATURE;

	ret = mlx5_calc_rwq_size(ctx, rwq, attr);
	if (ret < 0) {
		errno = -ret;
		goto err;
	}

	rwq->buf_size = ret;
	if (attr->comp_mask & IBV_EXP_CREATE_WQ_VLAN_OFFLOADS) {
		cmd.drv.comp_mask |= MLX5_EXP_CMD_CREATE_WQ_VLAN_OFFLOADS;
		cmd.drv.vlan_offloads = attr->vlan_offloads;
	}

	if (attr->comp_mask & IBV_EXP_CREATE_WQ_FLAGS) {
		if (attr->flags & IBV_EXP_CREATE_WQ_FLAG_RX_END_PADDING)
			cmd.drv.flags |= MLX5_EXP_RWQ_FLAGS_RX_END_PADDING;
		if (attr->flags & IBV_EXP_CREATE_WQ_FLAG_SCATTER_FCS)
			cmd.drv.flags |= MLX5_EXP_RWQ_FLAGS_SCATTER_FCS;
		if (attr->flags & IBV_EXP_CREATE_WQ_FLAG_DELAY_DROP)
			cmd.drv.flags |= MLX5_EXP_RWQ_FLAGS_DELAY_DROP;
	}

	if (attr->comp_mask & IBV_EXP_CREATE_WQ_MP_RQ) {
		/* Make sure requested mp_rq values supported by lib */
		if ((attr->mp_rq.single_stride_log_num_of_bytes > MLX5_MP_RQ_MAX_LOG_STRIDE_SIZE) ||
		    (attr->mp_rq.single_wqe_log_num_of_strides > MLX5_MP_RQ_MAX_LOG_NUM_STRIDES) ||
		    (attr->mp_rq.use_shift & ~MLX5_MP_RQ_SUPPORTED_SHIFTS)) {
			errno = EINVAL;
			goto err;
		}
		rsc_type = MLX5_RSC_TYPE_MP_RWQ;
		rwq->mp_rq_stride_size = 1 << attr->mp_rq.single_stride_log_num_of_bytes;
		rwq->mp_rq_strides_in_wqe = 1 << attr->mp_rq.single_wqe_log_num_of_strides;
		if (attr->mp_rq.use_shift == IBV_EXP_MP_RQ_2BYTES_SHIFT)
			rwq->mp_rq_packet_padding = 2;


		cmd.drv.mp_rq.use_shift = attr->mp_rq.use_shift;
		cmd.drv.mp_rq.single_stride_log_num_of_bytes = attr->mp_rq.single_stride_log_num_of_bytes;
		cmd.drv.mp_rq.single_wqe_log_num_of_strides = attr->mp_rq.single_wqe_log_num_of_strides;
		cmd.drv.mp_rq.reserved = 0;
		cmd.drv.comp_mask |= MLX5_EXP_CMD_CREATE_WQ_MP_RQ;
	} else {
		rsc_type = MLX5_RSC_TYPE_RWQ;
	}
	if (mlx5_alloc_rwq_buf(context, rwq, ret, rsc_type))
		goto err;

	mlx5_init_rwq_indices(rwq);

	if (attr->comp_mask & IBV_EXP_CREATE_WQ_RES_DOMAIN)
		thread_safe = (to_mres_domain(attr->res_domain)->attr.thread_model == IBV_EXP_THREAD_SAFE);

	rwq->model_flags = thread_safe ? MLX5_WQ_MODEL_FLAG_THREAD_SAFE : 0;

	memset(&device_attr, 0, sizeof(device_attr));
	device_attr.comp_mask = IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS;
	ret = ibv_exp_query_device(context, &device_attr);
	/* Check if RX offloads supported */
	if (!ret && (device_attr.comp_mask & IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS) &&
	    (device_attr.exp_device_cap_flags & IBV_EXP_DEVICE_RX_CSUM_IP_PKT))
		rwq->model_flags |= MLX5_WQ_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP;

	if (mlx5_lock_init(&rwq->rq.lock, thread_safe, mlx5_get_locktype()))
		goto err_free_rwq_buf;

	rwq->db = mlx5_alloc_dbrec(ctx);
	if (!rwq->db)
		goto err_free_rwq_buf;

	rwq->db[MLX5_RCV_DBR] = 0;
	rwq->db[MLX5_SND_DBR] = 0;
	rwq->rq.buff = rwq->buf.buf + rwq->rq.offset;
	rwq->rq.db = &rwq->db[MLX5_RCV_DBR];
	rwq->pattern = MLX5_WQ_PATTERN;

	cmd.drv.buf_addr = (uintptr_t)rwq->buf.buf;
	cmd.drv.db_addr  = (uintptr_t)rwq->db;
	cmd.drv.rq_wqe_count = rwq->rq.wqe_cnt;
	cmd.drv.rq_wqe_shift = rwq->rq.wqe_shift;
	cmd.drv.user_index = mlx5_store_uidx(ctx, rwq);
	if (cmd.drv.user_index < 0) {
		mlx5_dbg(fp, MLX5_DBG_QP, "Couldn't find free user index\n");
		goto err_free_db_rec;
	}

	err = ibv_exp_cmd_create_wq(context, attr, &rwq->wq, &cmd.ibv_cmd,
				    sizeof(cmd.ibv_cmd),
				    sizeof(cmd),
				    &resp.ibv_resp, sizeof(resp.ibv_resp),
				    sizeof(resp));
	if (err)
		goto err_create;

	rwq->rsc.type = rsc_type;
	rwq->rsc.rsn =  cmd.drv.user_index;

	return &rwq->wq;

err_create:
	mlx5_clear_uidx(ctx, cmd.drv.user_index);
err_free_db_rec:
	mlx5_free_db(to_mctx(context), rwq->db);
err_free_rwq_buf:
	mlx5_free_rwq_buf(rwq, context);
err:
	free(rwq);
	return NULL;
}

int mlx5_exp_modify_wq(struct ibv_exp_wq *wq,
		       struct ibv_exp_wq_attr *attr)
{
	struct mlx5_exp_modify_wq	cmd;
	struct mlx5_rwq *rwq = to_mrwq(wq);
	int ret;

	if ((attr->attr_mask & IBV_EXP_WQ_ATTR_STATE) &&
	    attr->wq_state == IBV_EXP_WQS_RDY) {
		if ((attr->attr_mask & IBV_EXP_WQ_ATTR_CURR_STATE) &&
		    attr->curr_wq_state != wq->state)
			return -EINVAL;

		if (wq->state == IBV_EXP_WQS_RESET) {
			mlx5_lock(&to_mcq(wq->cq)->lock);
			__mlx5_cq_clean(to_mcq(wq->cq),
					rwq->rsc.rsn, wq->srq ? to_msrq(wq->srq) : NULL);
			mlx5_unlock(&to_mcq(wq->cq)->lock);
			mlx5_init_rwq_indices(rwq);
			rwq->db[MLX5_RCV_DBR] = 0;
			rwq->db[MLX5_SND_DBR] = 0;
		}
	}

	memset(&cmd, 0, sizeof(cmd));
	if (attr->attr_mask & IBV_EXP_CREATE_WQ_VLAN_OFFLOADS) {
		cmd.drv.attr_mask |= MLX5_EXP_MODIFY_WQ_VLAN_OFFLOADS;
		cmd.drv.vlan_offloads = attr->vlan_offloads;
	}

	ret = ibv_exp_cmd_modify_wq(wq, attr, &cmd.ibv_cmd, sizeof(cmd));
	return ret;
}

int mlx5_exp_destroy_wq(struct ibv_exp_wq *wq)
{
	struct mlx5_rwq *rwq = to_mrwq(wq);
	int ret;

	ret = ibv_exp_cmd_destroy_wq(wq);
	if (ret) {
		pthread_mutex_unlock(&to_mctx(wq->context)->rsc_table_mutex);
		return ret;
	}

	mlx5_lock(&to_mcq(wq->cq)->lock);
	__mlx5_cq_clean(to_mcq(wq->cq), rwq->rsc.rsn,
			wq->srq ? to_msrq(wq->srq) : NULL);
	mlx5_unlock(&to_mcq(wq->cq)->lock);

	mlx5_clear_uidx(to_mctx(wq->context), rwq->rsc.rsn);
	mlx5_free_db(to_mctx(wq->context), rwq->db);
	mlx5_free_rwq_buf(rwq, wq->context);
	free(rwq);

	return 0;
}

struct ibv_exp_dct *mlx5_create_dct(struct ibv_context *context,
				    struct ibv_exp_dct_init_attr *attr)
{
	struct mlx5_create_dct		cmd;
	struct mlx5_create_dct_resp	resp;
	struct mlx5_destroy_dct		cmdd;
	struct mlx5_destroy_dct_resp	respd;
	int				err;
	struct mlx5_dct			*dct;
	struct mlx5_context		*ctx  = to_mctx(context);
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(context)->dbg_fp;
#endif

	memset(&cmd, 0, sizeof(cmd));
	memset(&cmdd, 0, sizeof(cmdd));
	memset(&resp, 0, sizeof(resp));
	dct = calloc(1, sizeof(*dct));
	if (!dct)
		return NULL;

	if (ctx->cqe_version) {
		cmd.drv.uidx = mlx5_store_uidx(ctx, dct);
		if (cmd.drv.uidx < 0) {
			mlx5_dbg(fp, MLX5_DBG_QP, "Couldn't find free user index\n");
			goto ex_err;
		}
	} else {
		pthread_mutex_lock(&ctx->rsc_table_mutex);
	}

	err = ibv_exp_cmd_create_dct(context, &dct->ibdct, attr, &cmd.ibv_cmd,
				     sizeof(cmd.ibv_cmd),
				     sizeof(cmd) - sizeof(cmd.ibv_cmd),
				     &resp.ibv_resp, sizeof(resp.ibv_resp),
				     sizeof(resp) - sizeof(resp.ibv_resp));
	if (err)
		goto err_uidx;

	dct->ibdct.handle = resp.ibv_resp.dct_handle;
	dct->ibdct.dct_num = resp.ibv_resp.dct_num;
	dct->ibdct.pd = attr->pd;
	dct->ibdct.cq = attr->cq;
	dct->ibdct.srq = attr->srq;

	if (!ctx->cqe_version) {
		err = mlx5_store_rsc(ctx, dct->ibdct.dct_num, dct);
		if (err)
			goto err_destroy;

		pthread_mutex_unlock(&ctx->rsc_table_mutex);
	}
	dct->rsc.type = MLX5_RSC_TYPE_DCT;
	dct->rsc.rsn = ctx->cqe_version ? cmd.drv.uidx :
					  resp.ibv_resp.dct_num;

	return &dct->ibdct;

err_destroy:
	if (ibv_exp_cmd_destroy_dct(context, &dct->ibdct,
				    &cmdd.ibv_cmd,
				    sizeof(cmdd.ibv_cmd),
				    sizeof(cmdd) - sizeof(cmdd.ibv_cmd),
				    &respd.ibv_resp, sizeof(respd.ibv_resp),
				    sizeof(respd) - sizeof(respd.ibv_resp)))
		fprintf(stderr, "failed to destory DCT\n");
err_uidx:
	if (ctx->cqe_version)
		mlx5_clear_uidx(ctx, cmd.drv.uidx);
	else
		pthread_mutex_unlock(&ctx->rsc_table_mutex);
ex_err:
	free(dct);
	return NULL;
}

int mlx5_destroy_dct(struct ibv_exp_dct *dct)
{
	struct mlx5_destroy_dct		cmd;
	struct mlx5_destroy_dct_resp	resp;
	int				err;
	struct mlx5_dct		       *mdct = to_mdct(dct);
	struct mlx5_context	       *ctx = to_mctx(dct->context);


	memset(&cmd, 0, sizeof(cmd));
	if (!ctx->cqe_version)
		pthread_mutex_lock(&ctx->rsc_table_mutex);
	cmd.ibv_cmd.dct_handle = dct->handle;
	err = ibv_exp_cmd_destroy_dct(dct->context, dct,
				      &cmd.ibv_cmd,
				      sizeof(cmd.ibv_cmd),
				      sizeof(cmd) - sizeof(cmd.ibv_cmd),
				      &resp.ibv_resp, sizeof(resp.ibv_resp),
				      sizeof(resp) - sizeof(resp.ibv_resp));
	if (err)
		goto ex_err;

	mlx5_cq_clean(to_mcq(dct->cq), mdct->rsc.rsn, to_msrq(dct->srq));
	if (ctx->cqe_version) {
		mlx5_clear_uidx(ctx, mdct->rsc.rsn);
	} else {
		mlx5_clear_rsc(to_mctx(dct->context), dct->dct_num);
		pthread_mutex_unlock(&ctx->rsc_table_mutex);
	}

	free(mdct);
	return 0;

ex_err:
	if (!ctx->cqe_version)
		pthread_mutex_unlock(&ctx->rsc_table_mutex);
	return err;
}

int mlx5_query_dct(struct ibv_exp_dct *dct, struct ibv_exp_dct_attr *attr)
{
	struct mlx5_query_dct		cmd;
	struct mlx5_query_dct_resp	resp;
	int				err;

	cmd.ibv_cmd.dct_handle = dct->handle;
	err = ibv_exp_cmd_query_dct(dct->context, &cmd.ibv_cmd,
				    sizeof(cmd.ibv_cmd),
				    sizeof(cmd) - sizeof(cmd.ibv_cmd),
				    &resp.ibv_resp, sizeof(resp.ibv_resp),
				    sizeof(resp) - sizeof(resp.ibv_resp),
				    attr);
	if (err)
		goto out;

	attr->cq = dct->cq;
	attr->pd = dct->pd;
	attr->srq = dct->srq;

out:
	return err;
}

int mlx5_arm_dct(struct ibv_exp_dct *dct, struct ibv_exp_arm_attr *attr)
{
	struct mlx5_arm_dct		cmd;
	struct mlx5_arm_dct_resp	resp;
	int				err;

	memset(&cmd, 0, sizeof(cmd));
	memset(&resp, 0, sizeof(resp));
	cmd.ibv_cmd.dct_handle = dct->handle;
	err = ibv_exp_cmd_arm_dct(dct->context, attr, &cmd.ibv_cmd,
				  sizeof(cmd.ibv_cmd),
				  sizeof(cmd) - sizeof(cmd.ibv_cmd),
				  &resp.ibv_resp, sizeof(resp.ibv_resp),
				  sizeof(resp) - sizeof(resp.ibv_resp));
	return err;
}

static void mlx5_lock_cqs(struct ibv_qp *qp)
{
	struct mlx5_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx5_cq *recv_cq = to_mcq(qp->recv_cq);

	if (send_cq && recv_cq) {
		if (send_cq == recv_cq) {
			mlx5_lock(&send_cq->lock);
		} else if (send_cq->cqn < recv_cq->cqn) {
			mlx5_lock(&send_cq->lock);
			mlx5_lock(&recv_cq->lock);
		} else {
			mlx5_lock(&recv_cq->lock);
			mlx5_lock(&send_cq->lock);
		}
	} else if (send_cq) {
		mlx5_lock(&send_cq->lock);
	} else if (recv_cq) {
		mlx5_lock(&recv_cq->lock);
	}
}

static void mlx5_unlock_cqs(struct ibv_qp *qp)
{
	struct mlx5_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx5_cq *recv_cq = to_mcq(qp->recv_cq);

	if (send_cq && recv_cq) {
		if (send_cq == recv_cq) {
			mlx5_unlock(&send_cq->lock);
		} else if (send_cq->cqn < recv_cq->cqn) {
			mlx5_unlock(&recv_cq->lock);
			mlx5_unlock(&send_cq->lock);
		} else {
			mlx5_unlock(&send_cq->lock);
			mlx5_unlock(&recv_cq->lock);
		}
	} else if (send_cq) {
		mlx5_unlock(&send_cq->lock);
	} else if (recv_cq) {
		mlx5_unlock(&recv_cq->lock);
	}
}

int mlx5_destroy_qp(struct ibv_qp *ibqp)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	struct mlx5_context *ctx = to_mctx(ibqp->context);
	int ret;

	if (qp->rx_qp) {
		ret = ibv_cmd_destroy_qp(ibqp);
		if (ret)
			return ret;
		goto free;
	}

	if (!ctx->cqe_version)
		pthread_mutex_lock(&ctx->rsc_table_mutex);

	ret = ibv_cmd_destroy_qp(ibqp);
	if (ret) {
		if (!ctx->cqe_version)
			pthread_mutex_unlock(&to_mctx(ibqp->context)->rsc_table_mutex);
		return ret;
	}

	mlx5_lock_cqs(ibqp);

	__mlx5_cq_clean(to_mcq(ibqp->recv_cq), qp->rsc.rsn,
			ibqp->srq ? to_msrq(ibqp->srq) : NULL);
	if (ibqp->send_cq != ibqp->recv_cq)
		__mlx5_cq_clean(to_mcq(ibqp->send_cq), qp->rsc.rsn, NULL);

	if (!ctx->cqe_version)
		mlx5_clear_rsc(ctx, ibqp->qp_num);

	mlx5_unlock_cqs(ibqp);
	if (!ctx->cqe_version)
		pthread_mutex_unlock(&ctx->rsc_table_mutex);
	else if (!is_xrc_tgt(ibqp->qp_type))
		mlx5_clear_uidx(ctx, qp->rsc.rsn);

	if (qp->peer_enabled)
		free_peer_va(qp);
	if (qp->peer_db_buf)
		qp->peer_ctx->buf_release(qp->peer_db_buf);
	else
		mlx5_free_db(ctx, qp->gen_data.db);
	mlx5_free_qp_buf(qp);

free:
	free(qp);

	return 0;
}

int mlx5_query_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		  int attr_mask, struct ibv_qp_init_attr *init_attr)
{
	struct ibv_query_qp cmd;
	struct mlx5_qp *qp = to_mqp(ibqp);
	int ret;

	if (qp->rx_qp)
		return -ENOSYS;

	ret = ibv_cmd_query_qp(ibqp, attr, attr_mask, init_attr, &cmd, sizeof(cmd));
	if (ret)
		return ret;

	init_attr->cap.max_send_wr     = qp->sq.max_post;
	init_attr->cap.max_send_sge    = qp->sq.max_gs;
	init_attr->cap.max_inline_data = qp->data_seg.max_inline_data;

	attr->cap = init_attr->cap;

	return 0;
}

static int update_port_data(struct ibv_qp *qp, uint8_t port_num)
{
	struct mlx5_context *ctx = to_mctx(qp->context);
	struct mlx5_qp *mqp = to_mqp(qp);
	struct ibv_port_attr port_attr;
	int err;

	err = ibv_query_port(qp->context, port_num, &port_attr);
	if (err)
		return err;

	mqp->link_layer = port_attr.link_layer;
	if ((((qp->qp_type == IBV_QPT_UD) && (mqp->link_layer == IBV_LINK_LAYER_INFINIBAND)) ||
	     ((qp->qp_type == IBV_QPT_RAW_ETH) && (mqp->link_layer == IBV_LINK_LAYER_ETHERNET))) &&
	    (ctx->exp_device_cap_flags & IBV_EXP_DEVICE_RX_CSUM_IP_PKT))
		mqp->gen_data.model_flags |= MLX5_QP_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP;

	return 0;
}

int mlx5_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		   int attr_mask)
{
	struct mlx5_qp *mqp = to_mqp(qp);
	struct mlx5_context *ctx = to_mctx(qp->context);
	struct ibv_modify_qp cmd;
	volatile uint32_t *db;
	int ret;

	if (mqp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) {
		if (attr_mask & ~(IBV_QP_STATE | IBV_QP_CUR_STATE))
			return EINVAL;

		/* Underlay QP is UD over infiniband */
		if (ctx->exp_device_cap_flags & IBV_EXP_DEVICE_RX_CSUM_IP_PKT)
			mqp->gen_data.model_flags |= MLX5_QP_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP;
	}

	if (attr_mask & IBV_QP_PORT) {
		ret = update_port_data(qp, attr->port_num);
		if (ret)
			return ret;
	}

	if (to_mqp(qp)->rx_qp)
		return -ENOSYS;

	ret = ibv_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof(cmd));

	if (!ret		       &&
	    (attr_mask & IBV_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RESET) {
		if (qp->recv_cq) {
			mlx5_cq_clean(to_mcq(qp->recv_cq), mqp->rsc.rsn,
				      qp->srq ? to_msrq(qp->srq) : NULL);
		}
		if (qp->send_cq != qp->recv_cq && qp->send_cq)
			mlx5_cq_clean(to_mcq(qp->send_cq), mqp->rsc.rsn, NULL);

		mlx5_init_qp_indices(mqp);
		db = mqp->gen_data.db;
		db[MLX5_RCV_DBR] = 0;
		db[MLX5_SND_DBR] = 0;
	}
	if (!ret && (attr_mask & IBV_QP_STATE))
		mlx5_update_post_send_one(mqp, qp->state, qp->qp_type);

	if (!ret &&
	    (attr_mask & IBV_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RTR &&
	    (qp->qp_type == IBV_QPT_RAW_ETH || mqp->flags & MLX5_QP_FLAGS_USE_UNDERLAY)) {
		mlx5_lock(&mqp->rq.lock);
		mqp->gen_data.db[MLX5_RCV_DBR] = htonl(mqp->rq.head & 0xffff);
		mlx5_unlock(&mqp->rq.lock);
	}


	return ret;
}

static inline int ipv6_addr_v4mapped(const struct in6_addr *a)
{
	return ((a->s6_addr32[0] | a->s6_addr32[1]) |
		(a->s6_addr32[2] ^ htonl(0x0000ffff))) == 0UL ||
		/* IPv4 encoded multicast addresses */
	       (a->s6_addr32[0]  == htonl(0xff0e0000) &&
		((a->s6_addr32[1] |
		  (a->s6_addr32[2] ^ htonl(0x0000ffff))) == 0UL));
}

struct ibv_ah *mlx5_create_ah_common(struct ibv_pd *pd,
				     struct ibv_ah_attr *attr,
				     uint8_t link_layer,
				     int gid_type)
{
	struct mlx5_ah *ah;
	struct mlx5_context *ctx = to_mctx(pd->context);
	struct mlx5_wqe_av *wqe;
	uint32_t tmp;
	uint8_t  grh;

	if (unlikely(attr->port_num < 1 || attr->port_num > ctx->num_ports)) {
		errno = EINVAL;
		return NULL;
	}

	if (unlikely(!attr->dlid) &&
	    (link_layer != IBV_LINK_LAYER_ETHERNET)) {
		errno = EINVAL;
		return NULL;
	}

	if (unlikely(!attr->is_global) &&
	    (link_layer == IBV_LINK_LAYER_ETHERNET)) {
		errno = EINVAL;
		return NULL;
	}

	ah = calloc(1, sizeof *ah);
	if (unlikely(!ah)) {
		errno = ENOMEM;
		return NULL;
	}
	wqe = &ah->av;

	if (link_layer == IBV_LINK_LAYER_ETHERNET) {
		if (gid_type == IBV_EXP_ROCE_V2_GID_TYPE)
			wqe->base.rlid = htons(ctx->rroce_udp_sport_min);
		grh = 0;
		wqe->base.stat_rate_sl = (attr->static_rate << 4) | ((attr->sl & 0x7) << 1);
	} else {
		wqe->base.fl_mlid = attr->src_path_bits & 0x7f;
		wqe->base.rlid = htons(attr->dlid);
		wqe->base.stat_rate_sl = (attr->static_rate << 4) | attr->sl;
		grh = 1;
	}

	if (attr->is_global) {
		wqe->base.dqp_dct = htonl(MLX5_EXTENDED_UD_AV);
		wqe->grh_sec.tclass = attr->grh.traffic_class;
		if ((attr->grh.hop_limit < 2) &&
		    (link_layer == IBV_LINK_LAYER_ETHERNET) &&
		    (gid_type != IBV_EXP_IB_ROCE_V1_GID_TYPE))
			wqe->grh_sec.hop_limit = 0xff;
		else
			wqe->grh_sec.hop_limit = attr->grh.hop_limit;
		tmp = htonl((grh << 30) |
			    ((attr->grh.sgid_index & 0xff) << 20) |
			    (attr->grh.flow_label & 0xfffff));
		wqe->grh_sec.grh_gid_fl = tmp;
		memcpy(wqe->grh_sec.rgid, attr->grh.dgid.raw, 16);
		if ((link_layer == IBV_LINK_LAYER_ETHERNET) &&
		    (gid_type != IBV_EXP_IB_ROCE_V1_GID_TYPE) &&
		    ipv6_addr_v4mapped((struct in6_addr *)attr->grh.dgid.raw))
			memset(wqe->grh_sec.rgid, 0, 12);
	} else if (!ctx->compact_av) {
		wqe->base.dqp_dct = htonl(MLX5_EXTENDED_UD_AV);
	}

	return &ah->ibv_ah;
}

struct ibv_ah *mlx5_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	struct ibv_exp_port_attr port_attr;

	port_attr.comp_mask = IBV_EXP_QUERY_PORT_ATTR_MASK1;
	port_attr.mask1 = IBV_EXP_QUERY_PORT_LINK_LAYER;

	if (ibv_exp_query_port(pd->context, attr->port_num, &port_attr))
		return NULL;

	return mlx5_create_ah_common(pd, attr,  port_attr.link_layer,
				     IBV_EXP_IB_ROCE_V1_GID_TYPE);
}

struct ibv_ah *mlx5_exp_create_ah(struct ibv_pd *pd,
				  struct ibv_exp_ah_attr *attr_ex)
{
	struct mlx5_ah *mah;
	struct ibv_ah *ah;
	struct ibv_exp_port_attr port_attr;
	struct ibv_exp_gid_attr gid_attr;

	gid_attr.comp_mask = IBV_EXP_QUERY_GID_ATTR_TYPE;
	if (ibv_exp_query_gid_attr(pd->context, attr_ex->port_num, attr_ex->grh.sgid_index,
				   &gid_attr))
		return NULL;

	port_attr.comp_mask = IBV_EXP_QUERY_PORT_ATTR_MASK1;
	port_attr.mask1 = IBV_EXP_QUERY_PORT_LINK_LAYER;

	if (ibv_exp_query_port(pd->context, attr_ex->port_num, &port_attr))
		return NULL;

	ah = mlx5_create_ah_common(pd, (struct ibv_ah_attr *)attr_ex,
				   port_attr.link_layer, gid_attr.type);

	if (!ah)
		return NULL;

	mah = to_mah(ah);

	/* ll_address.len == 0 means no ll address given */
	if (attr_ex->comp_mask & IBV_EXP_AH_ATTR_LL &&
	    0 != attr_ex->ll_address.len) {
		if (LL_ADDRESS_ETH != attr_ex->ll_address.type ||
		    port_attr.link_layer != IBV_LINK_LAYER_ETHERNET)
			goto err;

		/* link layer is ethernet */
		if (6 != attr_ex->ll_address.len ||
		    NULL == attr_ex->ll_address.address)
			goto err;

		memcpy(mah->av.grh_sec.rmac,
		       attr_ex->ll_address.address,
		       attr_ex->ll_address.len);
	}

	return ah;

err:
	free(ah);
	return NULL;
}

/* must be called only when link layer is ETHERNET */
struct ibv_ah *mlx5_exp_create_kah(struct ibv_pd *pd,
				   struct ibv_exp_ah_attr *attr_ex)
{
	struct mlx5_context *ctx = to_mctx(pd->context);
	struct ibv_exp_gid_attr gid_attr;
	struct ibv_ah *ah;

	if (!(ctx->cmds_supp_uhw & MLX5_USER_CMDS_SUPP_UHW_CREATE_AH))
		return NULL;

	gid_attr.comp_mask = IBV_EXP_QUERY_GID_ATTR_TYPE;
	if (ibv_exp_query_gid_attr(pd->context, attr_ex->port_num, attr_ex->grh.sgid_index,
				   &gid_attr))
		return NULL;


	ah = mlx5_create_ah_common(pd, (struct ibv_ah_attr *)attr_ex,
				   IBV_LINK_LAYER_ETHERNET, gid_attr.type);

	if (ah) {
		struct mlx5_create_ah_resp resp = {};
		struct mlx5_ah *mah = to_mah(ah);

		if (ibv_cmd_create_ah(pd, ah, (struct ibv_ah_attr *)attr_ex,
				      &resp.ibv_resp, sizeof(resp))) {
			mlx5_destroy_ah(ah);
			return NULL;
		}

		mah->kern_ah = 1;
		memcpy(mah->av.grh_sec.rmac, resp.dmac, ETHERNET_LL_SIZE);
	}

	return ah;
}

int mlx5_destroy_ah(struct ibv_ah *ah)
{
	struct mlx5_ah *mah = to_mah(ah);
	int err;

	if (mah->kern_ah) {
		err = ibv_cmd_destroy_ah(ah);
		if (err)
			return err;
	}

	free(mah);
	return 0;
}

int mlx5_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid)
{
	return ibv_cmd_attach_mcast(qp, gid, lid);
}

int mlx5_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid)
{
	return ibv_cmd_detach_mcast(qp, gid, lid);
}

struct ibv_xrcd	*mlx5_open_xrcd(struct ibv_context *context,
				struct ibv_xrcd_init_attr *xrcd_init_attr)
{
	int err;
	struct verbs_xrcd *xrcd;
	struct ibv_open_xrcd cmd = {0};
	struct ibv_open_xrcd_resp resp = {0};

	xrcd = calloc(1, sizeof(*xrcd));
	if (!xrcd)
		return NULL;

	err = ibv_cmd_open_xrcd(context, xrcd, sizeof(*xrcd), xrcd_init_attr,
				&cmd, sizeof(cmd), &resp, sizeof(resp));
	if (err) {
		free(xrcd);
		return NULL;
	}

	return &xrcd->xrcd;
}

struct ibv_srq *mlx5_create_xrc_srq(struct ibv_context *context,
				    struct ibv_srq_init_attr_ex *attr)
{
	int err;
	struct mlx5_create_srq_ex cmd;
	struct mlx5_create_srq_resp resp;
	struct mlx5_srq	*msrq;
	struct mlx5_context *ctx;
	struct ibv_srq *ibsrq;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(context)->dbg_fp;
#endif

	msrq = mlx5_alloc_srq(context, &attr->attr);
	if (!msrq)
		return NULL;

	msrq->is_xsrq = 1;
	ibsrq = (struct ibv_srq *)&msrq->vsrq;

	memset(&cmd, 0, sizeof(cmd));
	memset(&resp, 0, sizeof(resp));

	ctx = to_mctx(context);

	cmd.buf_addr = (uintptr_t) msrq->buf.buf;
	cmd.db_addr  = (uintptr_t) msrq->db;
	if (msrq->wq_sig)
		cmd.flags = MLX5_SRQ_FLAG_SIGNATURE;

	if (ctx->cqe_version) {
		cmd.uidx = mlx5_store_uidx(ctx, msrq);
		if (cmd.uidx < 0) {
			mlx5_dbg(fp, MLX5_DBG_QP, "Couldn't find free user index\n");
			goto err_free_srq;
		}
	} else {
		cmd.uidx = 0xffffff;
		pthread_mutex_lock(&ctx->srq_table_mutex);
	}

	err = ibv_cmd_create_srq_ex(context, &msrq->vsrq, sizeof(msrq->vsrq),
				    attr, &cmd.ibv_cmd, sizeof(cmd),
				    &resp.ibv_resp, sizeof(resp));
	if (err)
		goto err_free_uidx;

	if (!ctx->cqe_version) {
		err = mlx5_store_srq(to_mctx(context), resp.srqn, msrq);
		if (err)
			goto err_destroy;

		pthread_mutex_unlock(&ctx->srq_table_mutex);
	}

	msrq->srqn = resp.srqn;
	msrq->rsc.type = MLX5_RSC_TYPE_XSRQ;
	msrq->rsc.rsn = ctx->cqe_version ? cmd.uidx : resp.srqn;

	return ibsrq;

err_destroy:
	ibv_cmd_destroy_srq(ibsrq);
err_free_uidx:
	if (ctx->cqe_version)
		mlx5_clear_uidx(ctx, cmd.uidx);
	else
		pthread_mutex_unlock(&ctx->srq_table_mutex);
err_free_srq:
	mlx5_free_srq(context, msrq);

	return NULL;
}
struct ibv_srq *mlx5_create_srq_ex(struct ibv_context *context,
				   struct ibv_srq_init_attr_ex *attr)
{
	if (!(attr->comp_mask & IBV_SRQ_INIT_ATTR_TYPE) ||
	    (attr->srq_type == IBV_SRQT_BASIC))
		return mlx5_create_srq(attr->pd,
				       (struct ibv_srq_init_attr *)attr);
	else if (attr->srq_type == IBV_SRQT_XRC)
		return mlx5_create_xrc_srq(context, attr);

	return NULL;
}

static struct mlx5_qp *
create_cmd_qp(struct ibv_context *context,
	      struct ibv_exp_create_srq_attr *srq_attr,
	      struct ibv_srq *srq)
{
	struct ibv_exp_qp_init_attr init_attr = {};
	struct ibv_port_attr port_attr;
	struct ibv_modify_qp qcmd = {};
	struct ibv_qp_attr attr = {};
	struct ibv_query_port pcmd;
	struct ibv_qp *qp;
	int attr_mask;
	int port = 1;
	int ret;
#ifdef MLX5_DEBUG
	FILE *fp = to_mctx(context)->dbg_fp;
#endif

	ret = ibv_cmd_query_port(context, port, &port_attr,
				 &pcmd, sizeof(pcmd));
	if (ret) {
		mlx5_dbg(fp, MLX5_DBG_QP, "ret %d\n", ret);
		return NULL;
	}

	init_attr.qp_type = IBV_QPT_RC;
	init_attr.srq = srq;
	/* Command QP will be used to pass MLX5_OPCODE_TAG_MATCHING messages
	 * to add/remove tag matching list entries.
	 * WQ size is based on max_ops parameter holding max number of
	 * outstanding list operation.
	 */
	init_attr.cap.max_send_wr = srq_attr->tm_cap.max_ops;
	/* Tag matching list entry will point to single sge buffer */
	init_attr.cap.max_send_sge = 1;
	init_attr.comp_mask = IBV_QP_INIT_ATTR_PD;
	init_attr.pd = srq_attr->pd;
	init_attr.send_cq = srq_attr->cq;
	init_attr.recv_cq = srq_attr->cq;

	qp = create_qp(context, &init_attr, 0);
	if (!qp)
		return NULL;

	attr.qp_state = IBV_QPS_INIT;
	attr.port_num = port;
	attr_mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX
		  | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

	ret = ibv_cmd_modify_qp(qp, &attr, attr_mask, &qcmd, sizeof(qcmd));
	if (ret) {
		mlx5_dbg(fp, MLX5_DBG_QP, "ret %d\n", ret);
		goto err;
	}

	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = IBV_MTU_256;
	attr.dest_qp_num = qp->qp_num; /* Loopback */
	attr.ah_attr.dlid = port_attr.lid;
	attr.ah_attr.port_num = port;
	attr_mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU
		  | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN
		  | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

	ret = ibv_cmd_modify_qp(qp, &attr, attr_mask, &qcmd, sizeof(qcmd));
	if (ret) {
		mlx5_dbg(fp, MLX5_DBG_QP, "ret %d\n", ret);
		goto err;
	}

	attr.qp_state = IBV_QPS_RTS;
	attr_mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT
		  | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN
		  | IBV_QP_MAX_QP_RD_ATOMIC;

	ret = ibv_cmd_modify_qp(qp, &attr, attr_mask, &qcmd, sizeof(qcmd));
	if (ret) {
		mlx5_dbg(fp, MLX5_DBG_QP, "ret %d\n", ret);
		goto err;
	}

	return to_mqp(qp);

err:
	mlx5_destroy_qp(qp);
	return NULL;
}

struct ibv_srq *mlx5_exp_create_srq(struct ibv_context *context,
				    struct ibv_exp_create_srq_attr *attr)
{
	int required_comp_mask = IBV_EXP_CREATE_SRQ_CQ;
	struct mlx5_context *ctx = to_mctx(context);
	struct mlx5_exp_create_srq_resp resp = {};
	struct mlx5_exp_create_srq cmd = {};
	struct mlx5_srq	*msrq;
	int err;
#ifdef MLX5_DEBUG
	FILE *fp = ctx->dbg_fp;
#endif

	switch (attr->srq_type) {
	case IBV_EXP_SRQT_BASIC:
		return mlx5_create_srq(attr->pd, &attr->base);
	case IBV_EXP_SRQT_XRC:
		required_comp_mask |= IBV_EXP_CREATE_SRQ_XRCD;
		break;
	case IBV_EXP_SRQT_TAG_MATCHING:
		required_comp_mask |= IBV_EXP_CREATE_SRQ_TM;
		break;
	default:
		errno = EINVAL;
		return NULL;
	}

	if ((attr->comp_mask & required_comp_mask) != required_comp_mask) {
		errno = EINVAL;
		return NULL;
	}

	msrq = mlx5_alloc_srq(context, &attr->base.attr);
	if (!msrq)
		return NULL;

	msrq->is_xsrq = 1;

	cmd.buf_addr = (uintptr_t) msrq->buf.buf;
	cmd.db_addr  = (uintptr_t) msrq->db;
	if (msrq->wq_sig)
		cmd.flags = MLX5_SRQ_FLAG_SIGNATURE;

	if (ctx->cqe_version) {
		cmd.uidx = mlx5_store_uidx(ctx, msrq);
		if (cmd.uidx < 0) {
			mlx5_dbg(fp, MLX5_DBG_QP, "Couldn't find free user index\n");
			goto err;
		}
	} else {
		cmd.uidx = 0xffffff;
		pthread_mutex_lock(&ctx->srq_table_mutex);
	}

	if (attr->srq_type == IBV_EXP_SRQT_TAG_MATCHING) {
		cmd.max_num_tags = attr->tm_cap.max_num_tags;
		if (attr->comp_mask & IBV_EXP_CREATE_SRQ_DC_OFFLOAD_PARAMS) {
			if (attr->dc_offload_params->comp_mask)
				goto err_free_uidx;
			cmd.dc_op.pkey_index = attr->dc_offload_params->pkey_index;
			cmd.dc_op.path_mtu = attr->dc_offload_params->path_mtu;
			cmd.dc_op.sl = attr->dc_offload_params->sl;
			cmd.dc_op.max_rd_atomic = attr->dc_offload_params->max_rd_atomic;
			cmd.dc_op.min_rnr_timer = attr->dc_offload_params->min_rnr_timer;
			cmd.dc_op.timeout = attr->dc_offload_params->timeout;
			cmd.dc_op.retry_cnt = attr->dc_offload_params->retry_cnt;
			cmd.dc_op.rnr_retry = attr->dc_offload_params->rnr_retry;
			cmd.dc_op.dct_key = attr->dc_offload_params->dct_key;
			cmd.dc_op.ooo_caps = attr->dc_offload_params->ooo_caps;
			cmd.comp_mask |= MLX5_EXP_CREATE_SRQ_MASK_DC_OP;
		}
	}

	err = ibv_exp_cmd_create_srq(context, &msrq->vsrq, attr, &cmd.ibv_cmd,
				     sizeof(cmd.ibv_cmd), sizeof(cmd),
				     &resp.ibv_resp,
				     sizeof(resp.ibv_resp), sizeof(resp));
	if (err)
		goto err_free_uidx;

	if (attr->srq_type == IBV_EXP_SRQT_TAG_MATCHING) {
		int i;

		msrq->cmd_qp = create_cmd_qp(context, attr, &msrq->vsrq.srq);
		if (!msrq->cmd_qp)
			goto err_destroy;

		msrq->tm_list = calloc(attr->tm_cap.max_num_tags + 1,
				       sizeof(struct mlx5_tag_entry));
		if (!msrq->tm_list)
			goto err_free_cmd;
		for (i = 0; i < attr->tm_cap.max_num_tags; i++)
			msrq->tm_list[i].next = &msrq->tm_list[i + 1];
		msrq->tm_head = &msrq->tm_list[0];
		msrq->tm_tail = &msrq->tm_list[i];

		msrq->op = calloc(msrq->cmd_qp->sq.wqe_cnt,
				  sizeof(struct mlx5_srq_op));
		if (!msrq->op)
			goto err_free_tm;
		msrq->op_head = 0;
		msrq->op_tail = 0;
	}

	if (!ctx->cqe_version) {
		err = mlx5_store_srq(ctx, resp.srqn, msrq);
		if (err)
			goto err_free_tm;

		pthread_mutex_unlock(&ctx->srq_table_mutex);
	}

	msrq->srqn = resp.srqn;
	msrq->rsc.type = MLX5_RSC_TYPE_XSRQ;
	msrq->rsc.rsn = ctx->cqe_version ? cmd.uidx : resp.srqn;

	return &msrq->vsrq.srq;

err_free_tm:
	free(msrq->tm_list);
	free(msrq->op);
err_free_cmd:
	if (msrq->cmd_qp)
		mlx5_destroy_qp(&msrq->cmd_qp->verbs_qp.qp);
err_destroy:
	ibv_cmd_destroy_srq(&msrq->vsrq.srq);
err_free_uidx:
	if (ctx->cqe_version)
		mlx5_clear_uidx(ctx, cmd.uidx);
	else
		pthread_mutex_unlock(&ctx->srq_table_mutex);
err:
	mlx5_free_srq(context, msrq);
	return NULL;
}

int mlx5_get_srq_num(struct ibv_srq *srq, uint32_t *srq_num)
{
	struct mlx5_srq	*msrq = to_msrq(srq);

	*srq_num = msrq->srqn;

	return 0;
}

struct ibv_qp *mlx5_open_qp(struct ibv_context *context,
			    struct ibv_qp_open_attr *attr)
{
	struct ibv_open_qp cmd;
	struct ibv_create_qp_resp resp;
	struct mlx5_qp *qp;
	int ret;
	struct mlx5_context *ctx = to_mctx(context);

	qp = aligned_calloc(sizeof(*qp));
	if (!qp)
		return NULL;

	ret = ibv_cmd_open_qp(context, &qp->verbs_qp, sizeof(qp->verbs_qp),
			      attr, &cmd, sizeof(cmd), &resp, sizeof(resp));
	if (ret)
		goto err;

	if (!ctx->cqe_version) {
		pthread_mutex_lock(&ctx->rsc_table_mutex);
		if (mlx5_store_rsc(ctx, qp->verbs_qp.qp.qp_num, qp)) {
			pthread_mutex_unlock(&ctx->rsc_table_mutex);
			goto destroy;
		}
		pthread_mutex_unlock(&ctx->rsc_table_mutex);
	}

	return (struct ibv_qp *)&qp->verbs_qp;

destroy:
	ibv_cmd_destroy_qp(&qp->verbs_qp.qp);
err:
	free(qp);
	return NULL;
}

int mlx5_close_xrcd(struct ibv_xrcd *ib_xrcd)
{
	struct verbs_xrcd *xrcd = container_of(ib_xrcd, struct verbs_xrcd, xrcd);
	int ret;

	ret = ibv_cmd_close_xrcd(xrcd);
	if (!ret)
		free(xrcd);

	return ret;
}

int mlx5_modify_qp_ex(struct ibv_qp *qp, struct ibv_exp_qp_attr *attr,
		      uint64_t attr_mask)
{
	struct mlx5_qp *mqp = to_mqp(qp);
	struct mlx5_context *ctx = to_mctx(qp->context);
	struct ibv_exp_modify_qp cmd;
	volatile uint32_t *db;
	int ret;

	if (mqp->flags & MLX5_QP_FLAGS_USE_UNDERLAY) {
		if (attr_mask & ~(IBV_QP_STATE | IBV_QP_CUR_STATE))
			return EINVAL;

		/* Underlay QP is UD over infiniband */
		if (ctx->exp_device_cap_flags & IBV_EXP_DEVICE_RX_CSUM_IP_PKT)
			mqp->gen_data.model_flags |= MLX5_QP_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP;
	}

	if (attr_mask & IBV_QP_PORT) {
		ret = update_port_data(qp, attr->port_num);
		if (ret)
			return ret;
	}

	if (mqp->rx_qp)
		return -ENOSYS;

	memset(&cmd, 0, sizeof(cmd));
	ret = ibv_exp_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof(cmd));

	if (!ret		       &&
	    (attr_mask & IBV_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RESET) {
		if (qp->qp_type != IBV_EXP_QPT_DC_INI)
			mlx5_cq_clean(to_mcq(qp->recv_cq), mqp->rsc.rsn,
				      qp->srq ? to_msrq(qp->srq) : NULL);

		if (qp->send_cq != qp->recv_cq)
			mlx5_cq_clean(to_mcq(qp->send_cq), mqp->rsc.rsn, NULL);

		mlx5_init_qp_indices(to_mqp(qp));
		db = to_mqp(qp)->gen_data.db;
		db[MLX5_RCV_DBR] = 0;
		db[MLX5_SND_DBR] = 0;
	}
	if (!ret && (attr_mask & IBV_QP_STATE))
		mlx5_update_post_send_one(to_mqp(qp), qp->state, qp->qp_type);

	if (!ret &&
	    (attr_mask & IBV_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RTR &&
	    (qp->qp_type == IBV_QPT_RAW_ETH || mqp->flags & MLX5_QP_FLAGS_USE_UNDERLAY)) {
		mlx5_lock(&mqp->rq.lock);
		mqp->gen_data.db[MLX5_RCV_DBR] = htonl(mqp->rq.head & 0xffff);
		mlx5_unlock(&mqp->rq.lock);
	}

	return ret;
}

void *mlx5_get_legacy_xrc(struct ibv_srq *srq)
{
	struct mlx5_srq	*msrq = to_msrq(srq);

	return msrq->ibv_srq_legacy;
}

void mlx5_set_legacy_xrc(struct ibv_srq *srq, void *legacy_xrc_srq)
{
	struct mlx5_srq	*msrq = to_msrq(srq);

	msrq->ibv_srq_legacy = legacy_xrc_srq;
	return;
}

int mlx5_modify_cq(struct ibv_cq *cq, struct ibv_exp_cq_attr *attr, int attr_mask)
{
	struct ibv_exp_modify_cq cmd;

	memset(&cmd, 0, sizeof(cmd));
	return ibv_exp_cmd_modify_cq(cq, attr, attr_mask, &cmd, sizeof(cmd));
}

struct ibv_exp_mkey_list_container *mlx5_alloc_mkey_mem(struct ibv_exp_mkey_list_container_attr *attr)
{
	struct mlx5_klm_buf *klm;
	size_t alignment;
	size_t size;
	int err;
	int page_size = to_mdev(attr->pd->context->device)->page_size;

	if (attr->mkey_list_type !=
			IBV_EXP_MKEY_LIST_TYPE_INDIRECT_MR) {
		errno = ENOMEM;
		return NULL;
	}

	klm = calloc(1, sizeof(*klm));
	if (!klm) {
		errno = ENOMEM;
		return NULL;
	}

	alignment = max(page_size, MLX5_UMR_PTR_ALIGN);
	size = align(attr->max_klm_list_size * sizeof(struct mlx5_wqe_data_seg),
		     alignment);

	err =  posix_memalign(&klm->buf, alignment, size);
	if (err) {
		errno = ENOMEM;
		goto ex_klm;
	}

	memset(klm->buf, 0, size);
	klm->mr = ibv_reg_mr(attr->pd, klm->buf, size, 0);
	if (!klm->mr)
		goto ex_list;

	klm->ibv_klm_list.max_klm_list_size = attr->max_klm_list_size;
	klm->ibv_klm_list.context = klm->mr->context;

	return &klm->ibv_klm_list;

ex_list:
	free(klm->buf);
ex_klm:
	free(klm);
	return NULL;
}

int mlx5_free_mkey_mem(struct ibv_exp_mkey_list_container *mem)
{
	struct mlx5_klm_buf *klm;
	int err;

	klm = to_klm(mem);
	err = ibv_dereg_mr(klm->mr);
	if (err) {
		fprintf(stderr, "unreg klm failed\n");
		return err;
	}
	free(klm->buf);
	free(klm);
	return 0;
}

int mlx5_query_mkey(struct ibv_mr *mr, struct ibv_exp_mkey_attr *mkey_attr)
{
	struct mlx5_query_mkey		cmd;
	struct mlx5_query_mkey_resp	resp;
	int				err;

	memset(&cmd, 0, sizeof(cmd));
	err = ibv_exp_cmd_query_mkey(mr->context, mr, mkey_attr, &cmd.ibv_cmd,
				     sizeof(cmd.ibv_cmd), sizeof(cmd),
				     &resp.ibv_resp, sizeof(resp.ibv_resp),
				     sizeof(resp));

	return err;
};

struct ibv_mr *mlx5_create_mr(struct ibv_exp_create_mr_in *in)
{
	struct mlx5_create_mr		cmd;
	struct mlx5_create_mr_resp	resp;
	struct mlx5_mr		       *mr;
	int				err;

	if (in->attr.create_flags & IBV_EXP_MR_SIGNATURE_EN) {
		errno = EOPNOTSUPP;
		return NULL;
	}

	memset(&cmd, 0, sizeof(cmd));
	memset(&resp, 0, sizeof(resp));

	mr = calloc(1, sizeof(*mr));
	if (!mr)
		return NULL;

	err = ibv_exp_cmd_create_mr(in, &mr->ibv_mr, &cmd.ibv_cmd,
				    sizeof(cmd.ibv_cmd),
				    sizeof(cmd) - sizeof(cmd.ibv_cmd),
				    &resp.ibv_resp,
				    sizeof(resp.ibv_resp), sizeof(resp) - sizeof(resp.ibv_resp));
	if (err)
		goto out;

	return &mr->ibv_mr;

out:
	free(mr);
	return NULL;
};

int mlx5_exp_dereg_mr(struct ibv_mr *ibmr, struct ibv_exp_dereg_out *out)
{
	struct mlx5_mr *mr;

	if (ibmr->lkey == ODP_GLOBAL_R_LKEY || ibmr->lkey == ODP_GLOBAL_W_LKEY) {
		out->need_dofork = 0;
	} else {
		mr = to_mmr(ibmr);
		out->need_dofork = (mr->buf.type == MLX5_ALLOC_TYPE_CONTIG ||
				    mr->type == MLX5_ODP_MR || mr->type == MLX5_DM_MR) ? 0 : 1;
	}

	return mlx5_dereg_mr(ibmr);
}

struct mlx5_info_record {
	uint16_t	lid[30];
	uint32_t	seq_num;
};

int mlx5_poll_dc_info(struct ibv_context *context,
		      struct ibv_exp_dc_info_ent *ents,
		      int nent,
		      int port)
{
	struct mlx5_context *ctx = to_mctx(context);
	void *start;
	struct mlx5_port_info_ctx *pc;
	struct mlx5_info_record *cr;
	int i;
	int j;
	uint32_t seq;

	if (!ctx->cc.buf)
		return -ENOSYS;

	if (port < 1 || port > ctx->num_ports)
		return -EINVAL;

	pc = &ctx->cc.port[port - 1];
	start = ctx->cc.buf + 4096 * (port - 1);

	cr = start + (pc->consumer & 0xfff);
	for (i = 0; i < nent; i++) {
		seq = ntohl(cr->seq_num);
		/* The buffer is initialized to all ff. So if the HW did not write anything,
		   the condition below will cause a return without polling any record. */
		if ((seq & 0xfff) != (pc->consumer & 0xfff))
			return i;

		/* When the process comes to life, the buffer may alredy contain
		   valid records. The "steady" field allows the process to synchronize
		   and continue from there */
		if (pc->steady) {
			if (((pc->consumer >> 12) - 1) == (seq >> 12))
				return i;
		} else {
			pc->consumer = seq & 0xfffff000;
			pc->steady = 1;
		}

		/* make sure LIDs are read after we indentify a new record */
		rmb();
		ents[i].seqnum = seq;
		for (j = 0; j < 30; j++)
			ents[i].lid[j] = ntohs(cr->lid[j]);

		pc->consumer += 64;
		cr = start + (pc->consumer & 0xfff);
	}
	return i;
}

static struct mlx5_send_db_data *allocate_send_db(struct mlx5_context *ctx)
{
	struct mlx5_device *dev = to_mdev(ctx->ibv_ctx.device);
	struct mlx5_send_db_data *send_db = NULL;
	unsigned int db_idx;
	struct mlx5_wc_uar *wc_uar;
	int j;
	int i;


	mlx5_spin_lock(&ctx->send_db_lock);
	if (!list_empty(&ctx->send_wc_db_list)) {
		send_db = list_entry(ctx->send_wc_db_list.next, struct mlx5_send_db_data, list);
		list_del(&send_db->list);
	}
	mlx5_spin_unlock(&ctx->send_db_lock);

	if (!send_db) {
		/* Fill up more send_db objects */
		wc_uar = calloc(1, sizeof(*wc_uar));
		if (!wc_uar) {
			errno = ENOMEM;
			return NULL;
		}

		wc_uar->send_db_data = calloc(ctx->num_uars_per_page * MLX5_NUM_NON_FP_BFREGS_PER_UAR,
					      sizeof(*wc_uar->send_db_data));
		if (!wc_uar->send_db_data) {
			free(wc_uar);
			errno = ENOMEM;
			return NULL;
		}
		mlx5_spin_lock(&ctx->send_db_lock);
		/* One res_domain per UUAR */
		if (ctx->num_wc_uars >= ctx->max_ctx_res_domain / MLX5_NUM_NON_FP_BFREGS_PER_UAR) {
			errno = ENOMEM;
			goto out;
		}
		db_idx = ctx->num_wc_uars;
		wc_uar->uar = mlx5_uar_mmap(db_idx, MLX5_EXP_IB_MMAP_N_ALLOC_WC_CMD, dev->page_size, ctx->ibv_ctx.cmd_fd);
		if (wc_uar->uar == MAP_FAILED) {
			errno = ENOMEM;
			goto out;
		}
		ctx->num_wc_uars += ctx->num_uars_per_page;
		mlx5_spin_unlock(&ctx->send_db_lock);

		wc_uar->uar_idx = db_idx;
		for (i = 0; i < ctx->num_uars_per_page; i++) {
			for (j = 0; j < MLX5_NUM_NON_FP_BFREGS_PER_UAR; ++j) {
				int idx = i * MLX5_NUM_NON_FP_BFREGS_PER_UAR + j;
				wc_uar->send_db_data[idx].bf.reg = (wc_uar->uar + MLX5_ADAPTER_PAGE_SIZE * i) + MLX5_BF_OFFSET + (j * ctx->bf_reg_size);
				wc_uar->send_db_data[idx].bf.buf_size = ctx->bf_reg_size / 2;
				wc_uar->send_db_data[idx].bf.db_method = (mlx5_single_threaded && wc_auto_evict_size() == 64) ?
									MLX5_DB_METHOD_DEDIC_BF_1_THREAD : MLX5_DB_METHOD_DEDIC_BF;
				wc_uar->send_db_data[idx].bf.offset = 0;

				mlx5_lock_init(&wc_uar->send_db_data[idx].bf.lock,
					       0,
					       mlx5_get_locktype());

				wc_uar->send_db_data[idx].bf.need_lock = mlx5_single_threaded ? 0 : 1;
				/* Indicate that this BF UUAR is not from the static
				 * UUAR infrastructure
				 */
				wc_uar->send_db_data[idx].bf.uuarn = MLX5_EXP_INVALID_UUAR;
				wc_uar->send_db_data[idx].wc_uar = wc_uar;

				/* We provide the doorbell register at index 0 to the user so we avoid
				 * inserting it to the free list
				 */
				if (idx > 0) {
					mlx5_spin_lock(&ctx->send_db_lock);
					list_add(&wc_uar->send_db_data[idx].list, &ctx->send_wc_db_list);
					mlx5_spin_unlock(&ctx->send_db_lock);
				}
			}
		}
		/* Return the first send_db object to the caller */
		send_db = &wc_uar->send_db_data[0];

		mlx5_spin_lock(&ctx->send_db_lock);
		list_add(&wc_uar->list, &ctx->wc_uar_list);
		mlx5_spin_unlock(&ctx->send_db_lock);
	}

	return send_db;

out:
	mlx5_spin_unlock(&ctx->send_db_lock);
	free(wc_uar->send_db_data);
	free(wc_uar);

	return NULL;
}

struct ibv_exp_res_domain *mlx5_exp_create_res_domain(struct ibv_context *context,
						      struct ibv_exp_res_domain_init_attr *attr)
{
	struct mlx5_context *ctx = to_mctx(context);
	struct mlx5_res_domain *res_domain;

	if (attr->comp_mask >= IBV_EXP_RES_DOMAIN_RESERVED) {
		errno = EINVAL;
		return NULL;
	}

	if (!ctx->max_ctx_res_domain) {
		errno = ENOSYS;
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
	res_domain->attr.comp_mask = IBV_EXP_RES_DOMAIN_RESERVED - 1;

	res_domain->send_db = allocate_send_db(ctx);
	if (!res_domain->send_db) {
		if (res_domain->attr.msg_model == IBV_EXP_MSG_FORCE_LOW_LATENCY)
			goto err;
	} else {
		switch (res_domain->attr.thread_model) {
		case IBV_EXP_THREAD_SAFE:
			res_domain->send_db->bf.db_method = MLX5_DB_METHOD_BF;
			res_domain->send_db->bf.need_lock = 1;
			break;
		case IBV_EXP_THREAD_UNSAFE:
			res_domain->send_db->bf.db_method = MLX5_DB_METHOD_DEDIC_BF;
			res_domain->send_db->bf.need_lock = 0;
			break;
		case IBV_EXP_THREAD_SINGLE:
			if (wc_auto_evict_size() == 64) {
				res_domain->send_db->bf.db_method = MLX5_DB_METHOD_DEDIC_BF_1_THREAD;
				res_domain->send_db->bf.need_lock = 0;
			} else {
				res_domain->send_db->bf.db_method = MLX5_DB_METHOD_DEDIC_BF;
				res_domain->send_db->bf.need_lock = 0;
			}
			break;
		}
	}

	return &res_domain->ibv_res_domain;

err:
	free(res_domain);

	return NULL;
}

static void free_send_db(struct mlx5_context *ctx,
			 struct mlx5_send_db_data *send_db)
{
	/*
	 * Currently we free the resource domain UUAR to the local
	 * send_wc_db_list. In the future we may consider unmapping
	 * UAR which all its UUARs are free.
	 */
	mlx5_spin_lock(&ctx->send_db_lock);
	list_add(&send_db->list, &ctx->send_wc_db_list);
	mlx5_spin_unlock(&ctx->send_db_lock);
}

int mlx5_exp_destroy_res_domain(struct ibv_context *context,
				struct ibv_exp_res_domain *res_dom,
				struct ibv_exp_destroy_res_domain_attr *attr)
{
	struct mlx5_res_domain *res_domain;

	if (!res_dom)
		return EINVAL;

	res_domain = to_mres_domain(res_dom);
	if (res_domain->send_db)
		free_send_db(to_mctx(context), res_domain->send_db);

	free(res_domain);

	return 0;
}

void *mlx5_exp_query_intf(struct ibv_context *context, struct ibv_exp_query_intf_params *params,
			  enum ibv_exp_query_intf_status *status)
{
	void *family = NULL;
	struct mlx5_qp *qp;
	struct mlx5_cq *cq;
	struct mlx5_rwq *rwq;

	*status = IBV_EXP_INTF_STAT_OK;

	if (!params->obj) {
		errno = EINVAL;
		*status = IBV_EXP_INTF_STAT_INVAL_OBJ;
		return NULL;
	}

	switch (params->intf) {
	case IBV_EXP_INTF_QP_BURST:
		qp = to_mqp(params->obj);
		if (qp->gen_data_warm.pattern == MLX5_QP_PATTERN) {
			family = mlx5_get_qp_burst_family(qp, params, status);
			if (*status != IBV_EXP_INTF_STAT_OK) {
				fprintf(stderr, PFX "Failed to get QP burst family\n");
				errno = EINVAL;
			}
		} else {
			fprintf(stderr, PFX "Warning: non-valid QP passed to query interface 0x%x 0x%x\n", qp->gen_data_warm.pattern, MLX5_QP_PATTERN);
			*status = IBV_EXP_INTF_STAT_INVAL_OBJ;
			errno = EINVAL;
		}
		break;

	case IBV_EXP_INTF_CQ:
		cq = to_mcq(params->obj);
		if (cq->pattern == MLX5_CQ_PATTERN) {
			family = (void *)mlx5_get_poll_cq_family(cq, params, status);
		} else {
			fprintf(stderr, PFX "Warning: non-valid CQ passed to query interface\n");
			*status = IBV_EXP_INTF_STAT_INVAL_OBJ;
			errno = EINVAL;
		}
		break;

	case IBV_EXP_INTF_WQ:
		rwq = to_mrwq(params->obj);
		if (rwq->pattern == MLX5_WQ_PATTERN) {
			family = mlx5_get_wq_family(rwq, params, status);
			if (*status != IBV_EXP_INTF_STAT_OK) {
				fprintf(stderr, PFX "Failed to get WQ family\n");
				errno = EINVAL;
			}
		} else {
			fprintf(stderr, PFX "Warning: non-valid WQ passed to query interface\n");
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

int mlx5_exp_release_intf(struct ibv_context *context, void *intf,
			  struct ibv_exp_release_intf_params *params)
{
	return 0;
}

#define READL(ptr) (*((uint32_t *)(ptr)))
static int mlx5_read_clock(struct ibv_context *context, uint64_t *cycles)
{
	uint32_t clockhi, clocklo, clockhi1;
	int i;
	struct mlx5_context *ctx = to_mctx(context);

	if (!ctx->hca_core_clock)
		return -EOPNOTSUPP;

	/* Handle wraparound */
	for (i = 0; i < 2; i++) {
		clockhi = ntohl(READL(ctx->hca_core_clock));
		clocklo = ntohl(READL(ctx->hca_core_clock + 4));
		clockhi1 = ntohl(READL(ctx->hca_core_clock));
		if (clockhi == clockhi1)
			break;
	}

	*cycles = (uint64_t)(clockhi & 0x7fffffff) << 32 | (uint64_t)clocklo;

	return 0;
}

struct __mlx5_clock_info_v1 {
	__u32 sig;
	__u32 resv;
	__u64 nsec;
	__u64 cycles;
	__u64 frac;
	__u32 mult;
	__u32 shift;
	__u64 mask;
};

#define READ_ONCE(x)  (*((volatile typeof(x) *)&(x)))
static int mlx5_get_clock_info(struct ibv_context *context,
			       struct ibv_exp_clock_info *clock_info)
{
	struct mlx5_context *ctx = to_mctx(context);
	struct __mlx5_clock_info_v1 *ci = ctx->clock_info_page;
	uint32_t sig;
#ifdef MLX5_DEBUG
	FILE *fp = ctx->dbg_fp;
#endif

	if (!ci) {
		mlx5_dbg(fp, MLX5_DBG_GEN, "Warning: uninitialized timestamp subsystem\n");
		return -EINVAL;
	}

	do {
repeat:
		sig = READ_ONCE(ci->sig);
		if (unlikely(sig & 1)) {
			cpu_relax();
			goto repeat;
		}
		clock_info->nsec   = ci->nsec;
		clock_info->cycles = ci->cycles;
		clock_info->frac   = ci->frac;
		clock_info->mult   = ci->mult;
		clock_info->shift  = ci->shift;
		clock_info->mask   = ci->mask;
		rmb(); /* make sure new signature is updated */
	} while (unlikely(sig != ci->sig));

	clock_info->comp_mask = 0;

	return 0;
}

int mlx5_exp_query_values(struct ibv_context *context, int q_values,
			  struct ibv_exp_values *values)
{
	int err = 0;

	values->comp_mask = 0;

	if (q_values & IBV_EXP_VALUES_CLOCK_INFO) {
		err = mlx5_get_clock_info(context, &values->clock_info);
		if (err)
			return err;
		else
			values->comp_mask |= IBV_EXP_VALUES_CLOCK_INFO;
	}

	if (q_values & (IBV_EXP_VALUES_HW_CLOCK | IBV_EXP_VALUES_HW_CLOCK_NS)) {
		uint64_t cycles;

		err = mlx5_read_clock(context, &cycles);
		if (!err) {
			if (q_values & IBV_EXP_VALUES_HW_CLOCK) {
				values->hwclock = cycles;
				values->comp_mask |= IBV_EXP_VALUES_HW_CLOCK;
			}
			if (q_values & IBV_EXP_VALUES_HW_CLOCK_NS) {
				struct mlx5_context *ctx = to_mctx(context);

				values->hwclock_ns =
					(((uint64_t)values->hwclock &
					  ctx->core_clock.mask) *
					 ctx->core_clock.mult)
					>> ctx->core_clock.shift;
				values->comp_mask |= IBV_EXP_VALUES_HW_CLOCK_NS;
			}
		}
	}

	return err;
}

