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
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <sched.h>
#include <sys/param.h>

#ifndef HAVE_IBV_REGISTER_DRIVER
#include <sysfs/libsysfs.h>
#endif

#include "mlx5.h"
#include "mlx5-abi.h"
#include "ec.h"

#ifndef PCI_VENDOR_ID_MELLANOX
#define PCI_VENDOR_ID_MELLANOX			0x15b3
#endif

#ifndef CPU_AND
#define CPU_AND(x, y, z) do {} while (0)
#endif

#ifndef CPU_EQUAL
#define CPU_EQUAL(x, y) 1
#endif

#ifndef CPU_COUNT
#define CPU_COUNT(x) 0
#endif

#define HCA(v, d) \
	{ .vendor = PCI_VENDOR_ID_##v,			\
	  .device = d }

struct {
	unsigned		vendor;
	unsigned		device;
} hca_table[] = {
	HCA(MELLANOX, 4113),	/* MT27600 Connect-IB */
	HCA(MELLANOX, 4114),	/* MT27600 Connect-IB virtual function */
	HCA(MELLANOX, 4115),	/* ConnectX-4 */
	HCA(MELLANOX, 4116),	/* ConnectX-4 VF */
	HCA(MELLANOX, 4117),	/* ConnectX-4Lx */
	HCA(MELLANOX, 4118),	/* ConnectX-4Lx VF */
	HCA(MELLANOX, 4119),	/* ConnectX-5, PCIe 3.0 */
	HCA(MELLANOX, 4120),	/* ConnectX-5 VF */
	HCA(MELLANOX, 4121),	/* ConnectX-5, PCIe 4.0 */
	HCA(MELLANOX, 4122),	/* ConnectX-5, PCIe 4.0 VF */
	HCA(MELLANOX, 4123),    /* ConnectX-6 */
	HCA(MELLANOX, 4124),	/* ConnectX-6 VF */
	HCA(MELLANOX, 41682),	/* BlueField integrated ConnectX-5 network controller */
	HCA(MELLANOX, 41683),	/* BlueField integrated ConnectX-5 network controller VF */
};

uint32_t mlx5_debug_mask = 0;
int mlx5_freeze_on_error_cqe;

static struct ibv_context_ops mlx5_ctx_ops = {
	.query_device  = mlx5_query_device,
	.query_port    = mlx5_query_port,
	.alloc_pd      = mlx5_alloc_pd,
	.dealloc_pd    = mlx5_free_pd,
	.reg_mr	       = mlx5_reg_mr,
	.rereg_mr      = mlx5_rereg_mr,
	.dereg_mr      = mlx5_dereg_mr,
	.alloc_mw      = mlx5_alloc_mw,
	.dealloc_mw    = mlx5_dealloc_mw,
	.bind_mw       = mlx5_bind_mw,
	.create_cq     = mlx5_create_cq,
	.poll_cq       = mlx5_poll_cq,
	.req_notify_cq = mlx5_arm_cq,
	.cq_event      = mlx5_cq_event,
	.resize_cq     = mlx5_resize_cq,
	.destroy_cq    = mlx5_destroy_cq,
	.create_srq    = mlx5_create_srq,
	.modify_srq    = mlx5_modify_srq,
	.query_srq     = mlx5_query_srq,
	.destroy_srq   = mlx5_destroy_srq,
	.post_srq_recv = mlx5_post_srq_recv,
	.create_qp     = mlx5_create_qp,
	.query_qp      = mlx5_query_qp,
	.modify_qp     = mlx5_modify_qp,
	.destroy_qp    = mlx5_destroy_qp,
	.post_send     = mlx5_post_send,
	.post_recv     = mlx5_post_recv,
	.create_ah     = mlx5_create_ah,
	.destroy_ah    = mlx5_destroy_ah,
	.attach_mcast  = mlx5_attach_mcast,
	.detach_mcast  = mlx5_detach_mcast
};

static int read_number_from_line(const char *line, int *value)
{
	const char *ptr;

	ptr = strchr(line, ':');
	if (!ptr)
		return 1;

	++ptr;

	*value = atoi(ptr);
	return 0;
}

static int get_free_uidx(struct mlx5_context *ctx)
{
	int tind;
	int i;

	for (tind = 0; tind < MLX5_QP_TABLE_SIZE; tind++) {
		if (ctx->uidx_table[tind].refcnt < MLX5_QP_TABLE_MASK)
			break;
	}

	if (tind == MLX5_QP_TABLE_SIZE)
		return -1;

	if (!ctx->uidx_table[tind].refcnt)
		return (tind << MLX5_QP_TABLE_SHIFT);

	for (i = 0; i < MLX5_QP_TABLE_MASK + 1; i++) {
		if (!ctx->uidx_table[tind].table[i])
			break;
	}

	return (tind << MLX5_QP_TABLE_SHIFT) | i;
}

uint32_t mlx5_store_uidx(struct mlx5_context *ctx, void *rsc)
{
	int tind;
	int ret = -1;
	int uidx;

	pthread_mutex_lock(&ctx->uidx_table_mutex);
	uidx = get_free_uidx(ctx);
	if (uidx < 0)
		goto out;

	tind = uidx >> MLX5_QP_TABLE_SHIFT;

	if (!ctx->uidx_table[tind].refcnt) {
		ctx->uidx_table[tind].table = calloc(MLX5_QP_TABLE_MASK + 1,
						     sizeof(void *));
		if (!ctx->uidx_table[tind].table)
			goto out;
	}

	++ctx->uidx_table[tind].refcnt;
	ctx->uidx_table[tind].table[uidx & MLX5_QP_TABLE_MASK] = rsc;
	ret = uidx;

out:
	pthread_mutex_unlock(&ctx->uidx_table_mutex);
	return ret;
}

void mlx5_clear_uidx(struct mlx5_context *ctx, uint32_t uidx)
{
	int tind = uidx >> MLX5_QP_TABLE_SHIFT;

	pthread_mutex_lock(&ctx->uidx_table_mutex);

	if (!--ctx->uidx_table[tind].refcnt)
		free(ctx->uidx_table[tind].table);
	else
		ctx->uidx_table[tind].table[uidx & MLX5_QP_TABLE_MASK] = NULL;

	pthread_mutex_unlock(&ctx->uidx_table_mutex);
}

static int mlx5_is_sandy_bridge(int *num_cores)
{
	char line[128];
	FILE *fd;
	int rc = 0;
	int cur_cpu_family = -1;
	int cur_cpu_model = -1;

	fd = fopen("/proc/cpuinfo", "r");
	if (!fd)
		return 0;

	*num_cores = 0;

	while (fgets(line, 128, fd)) {
		int value;

		/* if this is information on new processor */
		if (!strncmp(line, "processor", 9)) {
			++*num_cores;

			cur_cpu_family = -1;
			cur_cpu_model  = -1;
		} else if (!strncmp(line, "cpu family", 10)) {
			if ((cur_cpu_family < 0) && (!read_number_from_line(line, &value)))
				cur_cpu_family = value;
		} else if (!strncmp(line, "model", 5)) {
			if ((cur_cpu_model < 0) && (!read_number_from_line(line, &value)))
				cur_cpu_model = value;
		}

		/* if this is a Sandy Bridge CPU */
		if ((cur_cpu_family == 6) &&
		    (cur_cpu_model == 0x2A || (cur_cpu_model == 0x2D) ))
			rc = 1;
	}

	fclose(fd);
	return rc;
}

/*
man cpuset

  This format displays each 32-bit word in hexadecimal (using ASCII characters "0" - "9" and "a" - "f"); words
  are filled with leading zeros, if required. For masks longer than one word, a comma separator is used between
  words. Words are displayed in big-endian order, which has the most significant bit first. The hex digits
  within a word are also in big-endian order.

  The number of 32-bit words displayed is the minimum number needed to display all bits of the bitmask, based on
  the size of the bitmask.

  Examples of the Mask Format:

     00000001                        # just bit 0 set
     40000000,00000000,00000000      # just bit 94 set
     000000ff,00000000               # bits 32-39 set
     00000000,000E3862               # 1,5,6,11-13,17-19 set

  A mask with bits 0, 1, 2, 4, 8, 16, 32, and 64 set displays as:

     00000001,00000001,00010117

  The first "1" is for bit 64, the second for bit 32, the third for bit 16, the fourth for bit 8, the fifth for
  bit 4, and the "7" is for bits 2, 1, and 0.
*/
static void mlx5_local_cpu_set(struct mlx5_context *ctx, cpu_set_t *cpu_set)
{
	char *p, buf[1024];
	char env_value[VERBS_MAX_ENV_VAL];
	uint32_t word;
	int i, k;
	struct ibv_context *context = &ctx->ibv_ctx;

	if (!ibv_exp_cmd_getenv(context, "MLX5_LOCAL_CPUS", env_value, sizeof(env_value)))
		strncpy(buf, env_value, sizeof(buf));
	else {
		char fname[MAXPATHLEN];
		FILE *fp;

		snprintf(fname, MAXPATHLEN, "/sys/class/infiniband/%s/device/local_cpus",
			 ibv_get_device_name(context->device));

		fp = fopen(fname, "r");
		if (!fp) {
			fprintf(stderr, PFX "Warning: can not get local cpu set: failed to open %s\n", fname);
			return;
		}
		if (!fgets(buf, sizeof(buf), fp)) {
			fprintf(stderr, PFX "Warning: can not get local cpu set: failed to read cpu mask\n");
			fclose(fp);
			return;
		}
		fclose(fp);
	}

	p = strrchr(buf, ',');
	if (!p)
		p = buf;

	i = 0;
	do {
		if (*p == ',') {
			*p = 0;
			p ++;
		}

		word = strtoul(p, 0, 16);

		for (k = 0; word; ++k, word >>= 1)
			if (word & 1)
				CPU_SET(k+i, cpu_set);

		if (p == buf)
			break;

		p = strrchr(buf, ',');
		if (!p)
			p = buf;

		i += 32;
	} while (i < CPU_SETSIZE);
}

static int mlx5_device_local_numa(struct mlx5_context *ctx)
{
	char buf[1024];
	struct ibv_context *context = &ctx->ibv_ctx;
	char fname[MAXPATHLEN];
	FILE *fp;

	snprintf(fname, MAXPATHLEN, "/sys/class/infiniband/%s/device/numa_node",
		 ibv_get_device_name(context->device));

	fp = fopen(fname, "r");
	if (!fp)
		return -1;

	if (!fgets(buf, sizeof(buf), fp)) {
		fclose(fp);
		return -1;
	}
	fclose(fp);

	return (int)strtoul(buf, 0, 0);
}

static int mlx5_enable_stall_cq(struct mlx5_context *ctx, int only_sb)
{
	cpu_set_t my_cpus, dev_local_cpus, result_set;
	int stall_enable;
	int ret;
	int num_cores;

	if (only_sb && !mlx5_is_sandy_bridge(&num_cores))
		return 0;

	/* by default disable stall on sandy bridge arch */
	stall_enable = 0;

	/*
	 * check if app is bound to cpu set that is inside
	 * of device local cpu set. Disable stalling if true
	 */

	/* use static cpu set - up to CPU_SETSIZE (1024) cpus/node */
	CPU_ZERO(&my_cpus);
	CPU_ZERO(&dev_local_cpus);
	CPU_ZERO(&result_set);
	ret = sched_getaffinity(0, sizeof(my_cpus), &my_cpus);
	if (ret == -1) {
		if (errno == EINVAL)
			fprintf(stderr, PFX "Warning: my cpu set is too small\n");
		else
			fprintf(stderr, PFX "Warning: failed to get my cpu set\n");
		goto out;
	}

	/* get device local cpu set */
	mlx5_local_cpu_set(ctx, &dev_local_cpus);

	/* make sure result_set is not init to all 0 */
	CPU_SET(0, &result_set);
	/* Set stall_enable if my cpu set and dev cpu set are disjoint sets */
	CPU_AND(&result_set, &my_cpus, &dev_local_cpus);
	stall_enable = CPU_COUNT(&result_set) ? 0 : 1;

out:
	return stall_enable;
}

static void mlx5_read_env(struct mlx5_context *ctx)
{
	char env_value[VERBS_MAX_ENV_VAL];
	struct ibv_context *context = &ctx->ibv_ctx;

	/* If MLX5_STALL_CQ_POLL is not set enable stall CQ only on sandy bridge */
	if (ibv_exp_cmd_getenv(context, "MLX5_STALL_CQ_POLL", env_value, sizeof(env_value)))
		ctx->stall_enable = mlx5_enable_stall_cq(ctx, 1);
	/* If MLX5_STALL_CQ_POLL == 0 disable stall CQ */
	else if (!strcmp(env_value, "0"))
		ctx->stall_enable = 0;
	/* If MLX5_STALL_CQ_POLL == 1 enable stall CQ */
	else if (!strcmp(env_value, "1"))
		ctx->stall_enable = mlx5_enable_stall_cq(ctx, 0);
	/* Otherwise enable stall CQ only on sandy bridge */
	else
		ctx->stall_enable = mlx5_enable_stall_cq(ctx, 1);

	if (!ibv_exp_cmd_getenv(context, "MLX5_STALL_NUM_LOOP", env_value, sizeof(env_value)))
		mlx5_stall_num_loop = atoi(env_value);

	if (!ibv_exp_cmd_getenv(context, "MLX5_STALL_CQ_POLL_MIN", env_value, sizeof(env_value)))
		mlx5_stall_cq_poll_min = atoi(env_value);

	if (!ibv_exp_cmd_getenv(context, "MLX5_STALL_CQ_POLL_MAX", env_value, sizeof(env_value)))
		mlx5_stall_cq_poll_max = atoi(env_value);

	if (!ibv_exp_cmd_getenv(context, "MLX5_STALL_CQ_INC_STEP", env_value, sizeof(env_value)))
		mlx5_stall_cq_inc_step = atoi(env_value);

	if (!ibv_exp_cmd_getenv(context, "MLX5_STALL_CQ_DEC_STEP", env_value, sizeof(env_value)))
		mlx5_stall_cq_dec_step = atoi(env_value);

	ctx->stall_adaptive_enable = 0;
	ctx->stall_cycles = 0;
	ctx->numa_id = mlx5_device_local_numa(ctx);

	if (mlx5_stall_num_loop < 0) {
		ctx->stall_adaptive_enable = 1;
		ctx->stall_cycles = mlx5_stall_cq_poll_min;
	}
}

static int get_total_uuars(int page_size)
{
	int size = MLX5_DEF_TOT_UUARS;
	int uuars_in_page;

	uuars_in_page = page_size / MLX5_ADAPTER_PAGE_SIZE * MLX5_NUM_NON_FP_BFREGS_PER_UAR;
	size = max(uuars_in_page, size);
	size = align(size, MLX5_NUM_NON_FP_BFREGS_PER_UAR);
	if (size > MLX5_MAX_BFREGS)
		return -ENOMEM;

	return size;
}

static void open_debug_file(struct mlx5_context *ctx)
{
	char env[VERBS_MAX_ENV_VAL];

	if (ibv_exp_cmd_getenv(&ctx->ibv_ctx, "MLX5_DEBUG_FILE", env, sizeof(env))) {
		ctx->dbg_fp = stderr;
		return;
	}

	ctx->dbg_fp = fopen(env, "aw+");
	if (!ctx->dbg_fp) {
		fprintf(stderr, "Failed opening debug file %s, using stderr\n", env);
		ctx->dbg_fp = stderr;
		return;
	}
}

static void close_debug_file(struct mlx5_context *ctx)
{
	if (ctx->dbg_fp && ctx->dbg_fp != stderr)
		fclose(ctx->dbg_fp);
}

static void set_debug_mask(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(context, "MLX5_DEBUG_MASK", env, sizeof(env)))
		mlx5_debug_mask = strtol(env, NULL, 0);
}

static void set_freeze_on_error(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(context, "MLX5_FREEZE_ON_ERROR_CQE", env, sizeof(env)))
		mlx5_freeze_on_error_cqe = strtol(env, NULL, 0);
}

static int get_always_bf(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (ibv_exp_cmd_getenv(context, "MLX5_POST_SEND_PREFER_BF", env, sizeof(env)))
		return 1;

	return strcmp(env, "0") ? 1 : 0;
}

static int get_shut_up_bf(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (ibv_exp_cmd_getenv(context, "MLX5_SHUT_UP_BF", env, sizeof(env)))
		return 0;

	return strcmp(env, "0") ? 1 : 0;
}

static int get_cqe_comp(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (ibv_exp_cmd_getenv(context, "MLX5_ENABLE_CQE_COMPRESSION", env, sizeof(env)))
		return 0;

	return strcmp(env, "0") ? 1 : 0;
}

static int get_use_mutex(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (ibv_exp_cmd_getenv(context, "MLX5_USE_MUTEX", env, sizeof(env)))
		return 0;

	return strcmp(env, "0") ? 1 : 0;
}

static int get_num_low_lat_uuars(int tot_uuars)
{
	return max(4, tot_uuars - MLX5_MED_BFREGS_TSHOLD);
}

static int need_uuar_lock(struct mlx5_context *ctx, int uuarn)
{
	if (uuarn == 0)
		return 0;

	if (uuarn >= (ctx->tot_uuars - ctx->low_lat_uuars) * 2)
		return 0;

	return 1;
}

static int single_threaded_app(struct ibv_context *context)
{
	char env[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(context, "MLX5_SINGLE_THREADED", env, sizeof(env)))
		return strcmp(env, "1") ? 0 : 1;

	return 0;
}

static void set_extended(struct verbs_context *verbs_ctx)
{
	int off_create_qp_ex = offsetof(struct verbs_context, create_qp_ex);
	int off_open_xrcd = offsetof(struct verbs_context, open_xrcd);
	int off_create_srq = offsetof(struct verbs_context, create_srq_ex);
	int off_get_srq_num = offsetof(struct verbs_context, get_srq_num);
	int off_open_qp = offsetof(struct verbs_context, open_qp);
	int off_mlx5_close_xrcd = offsetof(struct verbs_context, close_xrcd);
	int off_create_flow = offsetof(struct verbs_context, create_flow);
	int off_destroy_flow = offsetof(struct verbs_context, destroy_flow);

	if (sizeof(*verbs_ctx) - off_create_qp_ex <= verbs_ctx->sz)
		verbs_ctx->create_qp_ex = mlx5_drv_create_qp;

	if (sizeof(*verbs_ctx) - off_open_xrcd <= verbs_ctx->sz)
		verbs_ctx->open_xrcd = mlx5_open_xrcd;

	if (sizeof(*verbs_ctx) - off_create_srq <= verbs_ctx->sz)
		verbs_ctx->create_srq_ex = mlx5_create_srq_ex;

	if (sizeof(*verbs_ctx) - off_get_srq_num <= verbs_ctx->sz)
		verbs_ctx->get_srq_num = mlx5_get_srq_num;

	if (sizeof(*verbs_ctx) - off_open_qp <= verbs_ctx->sz)
		verbs_ctx->open_qp = mlx5_open_qp;

	if (sizeof(*verbs_ctx) - off_mlx5_close_xrcd <= verbs_ctx->sz)
		verbs_ctx->close_xrcd = mlx5_close_xrcd;

	if (sizeof(*verbs_ctx) - off_create_flow <= verbs_ctx->sz)
		verbs_ctx->create_flow = ibv_cmd_create_flow;

	if (sizeof(*verbs_ctx) - off_destroy_flow <= verbs_ctx->sz)
		verbs_ctx->destroy_flow = ibv_cmd_destroy_flow;

	verbs_set_ctx_op(verbs_ctx, query_device_ex, mlx5_query_device_ex);
}

static void set_experimental(struct ibv_context *ctx)
{
	struct verbs_context_exp *verbs_exp_ctx = verbs_get_exp_ctx(ctx);
	struct mlx5_context *mctx = to_mctx(ctx);

	verbs_set_exp_ctx_op(verbs_exp_ctx, create_dct, mlx5_create_dct);
	verbs_set_exp_ctx_op(verbs_exp_ctx, destroy_dct, mlx5_destroy_dct);
	verbs_set_exp_ctx_op(verbs_exp_ctx, query_dct, mlx5_query_dct);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_arm_dct, mlx5_arm_dct);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_query_device, mlx5_exp_query_device);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_create_qp, mlx5_exp_create_qp);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_modify_qp, mlx5_modify_qp_ex);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_get_legacy_xrc, mlx5_get_legacy_xrc);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_set_legacy_xrc, mlx5_set_legacy_xrc);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_modify_cq, mlx5_modify_cq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_create_cq, mlx5_create_cq_ex);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_poll_cq, mlx5_poll_cq_ex);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_post_task, mlx5_post_task);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_reg_mr, mlx5_exp_reg_mr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_post_send, mlx5_exp_post_send);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_alloc_mkey_list_memory, mlx5_alloc_mkey_mem);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_dealloc_mkey_list_memory, mlx5_free_mkey_mem);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_query_mkey, mlx5_query_mkey);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_create_mr, mlx5_create_mr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_prefetch_mr,
			     mlx5_prefetch_mr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_dereg_mr, mlx5_exp_dereg_mr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_poll_dc_info, mlx5_poll_dc_info);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_create_wq, mlx5_exp_create_wq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_modify_wq, mlx5_exp_modify_wq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_destroy_wq, mlx5_exp_destroy_wq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_create_flow, ibv_exp_cmd_create_flow);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_destroy_flow, ibv_exp_cmd_destroy_flow);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_create_rwq_ind_table, mlx5_exp_create_rwq_ind_table);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_destroy_rwq_ind_table, mlx5_exp_destroy_rwq_ind_table);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_create_res_domain, mlx5_exp_create_res_domain);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_destroy_res_domain, mlx5_exp_destroy_res_domain);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_query_intf, mlx5_exp_query_intf);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_release_intf, mlx5_exp_release_intf);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_query_port, mlx5_exp_query_port);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_create_ah, mlx5_exp_create_ah);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_create_kah, mlx5_exp_create_kah);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_query_values, mlx5_exp_query_values);
	verbs_set_exp_ctx_op(verbs_exp_ctx, alloc_ec_calc, mlx5_alloc_ec_calc);
	verbs_set_exp_ctx_op(verbs_exp_ctx, dealloc_ec_calc, mlx5_dealloc_ec_calc);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_encode_async, mlx5_ec_encode_async);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_encode_sync, mlx5_ec_encode_sync);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_decode_async, mlx5_ec_decode_async);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_decode_sync, mlx5_ec_decode_sync);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_poll, mlx5_ec_poll);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_encode_send, mlx5_ec_encode_send);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_update_async, mlx5_ec_update_async);
	verbs_set_exp_ctx_op(verbs_exp_ctx, ec_update_sync, mlx5_ec_update_sync);
	if (mctx->cqe_version == 1)
		verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_poll_cq,
				     mlx5_poll_cq_ex_1);
	else
		verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_poll_cq,
				     mlx5_poll_cq_ex);

	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_peer_commit_qp, mlx5_exp_peer_commit_qp);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_rollback_send, mlx5_exp_rollback_send);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_peer_peek_cq, mlx5_exp_peer_peek_cq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_peer_abort_peek_cq, mlx5_exp_peer_abort_peek_cq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_set_context_attr, mlx5_exp_set_context_attr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_create_srq,
			     mlx5_exp_create_srq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_post_srq_ops,
			     mlx5_exp_post_srq_ops);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_alloc_dm, mlx5_exp_alloc_dm);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_free_dm, mlx5_exp_free_dm);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_memcpy_dm, mlx5_exp_memcpy_dm);
}

void *mlx5_uar_mmap(int idx, int cmd, int page_size, int cmd_fd)
{
	off_t offset;

	offset = 0;
	set_command(cmd, &offset);
	set_index(idx, &offset);

	return mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, cmd_fd, page_size * offset);
}

void read_init_vars(struct mlx5_context *ctx)
{
	pthread_mutex_lock(&ctx->env_mtx);
	if (!ctx->env_initialized) {
		mlx5_single_threaded = single_threaded_app(&ctx->ibv_ctx);
		mlx5_use_mutex = get_use_mutex(&ctx->ibv_ctx);
		open_debug_file(ctx);
		set_debug_mask(&ctx->ibv_ctx);
		set_freeze_on_error(&ctx->ibv_ctx);
		ctx->prefer_bf = get_always_bf(&ctx->ibv_ctx);
		ctx->shut_up_bf = get_shut_up_bf(&ctx->ibv_ctx);
		mlx5_read_env(ctx);
		ctx->env_initialized = 1;
	}
	pthread_mutex_unlock(&ctx->env_mtx);
}

static void mlx5_map_internal_clock(struct mlx5_device *mdev,
				    struct ibv_context *ibv_ctx)
{
	struct mlx5_context *context = to_mctx(ibv_ctx);
	void *hca_clock_page;
	off_t offset = 0;

	set_command(MLX5_EXP_MMAP_GET_CORE_CLOCK_CMD, &offset);
	hca_clock_page = mmap(NULL, mdev->page_size,
			      PROT_READ, MAP_SHARED, ibv_ctx->cmd_fd,
			      offset * mdev->page_size);

	if (hca_clock_page == MAP_FAILED)
		fprintf(stderr, PFX "Timestamp available but failed to mmap() hca core clock page.\n");
	else
		context->hca_core_clock = hca_clock_page +
					  context->core_clock.offset;

	return;
}

static void mlx5_map_clock_info(struct mlx5_device *mdev,
				struct ibv_context *ibv_ctx)
{
	struct mlx5_context *context = to_mctx(ibv_ctx);
	void *clock_info_page;
	off_t offset = 0;

	set_command(MLX5_EXP_IB_MMAP_CLOCK_INFO_CMD, &offset);
	set_index(MLX5_EXP_CLOCK_INFO_V1, &offset);
	clock_info_page = mmap(NULL, mdev->page_size,
			       PROT_READ, MAP_SHARED, ibv_ctx->cmd_fd,
			       offset * mdev->page_size);
	if (clock_info_page != MAP_FAILED)
		context->clock_info_page = clock_info_page;

	return;
}

int mlx5dv_query_device(struct ibv_context *ctx_in,
			 struct mlx5dv_context *attrs_out)
{
	attrs_out->comp_mask = 0;
	attrs_out->version   = 0;
	attrs_out->flags     = 0;

	if (to_mctx(ctx_in)->cqe_version == MLX5_CQE_VERSION_V1)
		attrs_out->flags |= MLX5DV_CONTEXT_FLAGS_CQE_V1;

	return 0;
}

static int mlx5dv_get_qp(struct ibv_qp *qp_in,
			 struct mlx5dv_qp *qp_out)
{
	struct mlx5_qp *mqp = to_mqp(qp_in);

	qp_out->comp_mask = 0;
	qp_out->dbrec     = mqp->gen_data.db;

	if (mqp->sq_buf_size)
		/* IBV_QPT_RAW_PACKET */
		qp_out->sq.buf = (void *)((uintptr_t)mqp->sq_buf.buf);
	else
		qp_out->sq.buf = (void *)((uintptr_t)mqp->buf.buf + mqp->sq.offset);
	qp_out->sq.wqe_cnt = mqp->sq.wqe_cnt;
	qp_out->sq.stride  = 1 << mqp->sq.wqe_shift;

	qp_out->rq.buf     = (void *)((uintptr_t)mqp->buf.buf + mqp->rq.offset);
	qp_out->rq.wqe_cnt = mqp->rq.wqe_cnt;
	qp_out->rq.stride  = 1 << mqp->rq.wqe_shift;

	qp_out->bf.reg    = mqp->gen_data.bf->reg;

	if (mqp->gen_data.bf->uuarn > 0)
		qp_out->bf.size = mqp->gen_data.bf->buf_size;
	else
		qp_out->bf.size = 0;

	return 0;
}

static int mlx5dv_get_cq(struct ibv_cq *cq_in,
			 struct mlx5dv_cq *cq_out)
{
	struct mlx5_cq *mcq = to_mcq(cq_in);
	struct mlx5_context *mctx = to_mctx(cq_in->context);

	cq_out->comp_mask = 0;
	cq_out->cqn       = mcq->cqn;
	cq_out->cqe_cnt   = mcq->ibv_cq.cqe + 1;
	cq_out->cqe_size  = mcq->cqe_sz;
	cq_out->buf       = mcq->active_buf->buf;
	cq_out->dbrec     = mcq->dbrec;
	cq_out->uar	  = mctx->uar;

	mcq->model_flags  |= MLX5_CQ_MODEL_FLAG_DV_OWNED;

	return 0;
}

static int mlx5dv_get_rwq(struct ibv_exp_wq *wq_in,
			  struct mlx5dv_rwq *rwq_out)
{
	struct mlx5_rwq *mrwq = to_mrwq(wq_in);

	rwq_out->comp_mask = 0;
	rwq_out->buf       = mrwq->buf.buf + mrwq->rq.offset;
	rwq_out->dbrec     = mrwq->db;
	rwq_out->wqe_cnt   = mrwq->rq.wqe_cnt;
	rwq_out->stride    = 1 << mrwq->rq.wqe_shift;

	return 0;
}

static int mlx5dv_get_srq(struct ibv_srq *srq_in,
			  struct mlx5dv_srq *srq_out)
{
	struct mlx5_srq *msrq;

	msrq = container_of(srq_in, struct mlx5_srq, vsrq.srq);

	srq_out->comp_mask = 0;
	srq_out->buf       = msrq->buf.buf;
	srq_out->dbrec     = msrq->db;
	srq_out->stride    = 1 << msrq->wqe_shift;
	srq_out->head      = msrq->head;
	srq_out->tail      = msrq->tail;

	return 0;
}

int mlx5dv_init_obj(struct mlx5dv_obj *obj, uint64_t obj_type)
{
	int ret = 0;

	if (obj_type & MLX5DV_OBJ_QP)
		ret = mlx5dv_get_qp(obj->qp.in, obj->qp.out);
	if (!ret && (obj_type & MLX5DV_OBJ_CQ))
		ret = mlx5dv_get_cq(obj->cq.in, obj->cq.out);
	if (!ret && (obj_type & MLX5DV_OBJ_SRQ))
		ret = mlx5dv_get_srq(obj->srq.in, obj->srq.out);
	if (!ret && (obj_type & MLX5DV_OBJ_RWQ))
		ret = mlx5dv_get_rwq(obj->rwq.in, obj->rwq.out);

	return ret;
}

enum mlx5_cap_flags {
	MLX5_CAP_COMPACT_AV	= 1 << 0,
	MLX5_CAP_ODP_IMPLICIT	= 1 << 1,
};

int mlx5_exp_set_context_attr(struct ibv_context *context,
			      struct ibv_exp_open_device_attr *attr)
{
	struct ibv_exp_cmd_set_context_attr cmd;

	memset(&cmd, 0, sizeof(cmd));
	return ibv_exp_cmd_set_context_attr(context, attr, &cmd,
					    sizeof(cmd));
}

static void adjust_uar_info(struct mlx5_device *mdev,
			    struct mlx5_context *context,
			    struct mlx5_exp_alloc_ucontext_resp resp)
{
	if (!resp.log_uar_size && !resp.num_uars_per_page) {
		/* old kernel */
		context->uar_size = mdev->page_size;
		context->num_uars_per_page = 1;
		return;
	}

	context->uar_size = 1 << resp.log_uar_size;
	context->num_uars_per_page = resp.num_uars_per_page;
}

static int mlx5_alloc_context(struct verbs_device *vdev,
			      struct ibv_context *ctx, int cmd_fd)
{
	struct mlx5_context	       *context;
	struct mlx5_alloc_ucontext	req;
	struct mlx5_exp_alloc_ucontext_resp resp;
	struct ibv_device		*ibdev = &vdev->device;
	struct verbs_context *verbs_ctx = verbs_get_ctx(ctx);
	struct ibv_exp_device_attr attr;
	int	num_sys_page_map;
	int	i;
	int	page_size = to_mdev(ibdev)->page_size;
	int	tot_uuars;
	int	low_lat_uuars;
	int	gross_uuars;
	int	j;
	int	uar_mapped;
	off_t	offset;
	int	err;
	int	legacy_uar_map = 0;
	int	bfi;
	int	k;

	context = to_mctx(ctx);
	if (pthread_mutex_init(&context->env_mtx, NULL))
		return -1;

	context->ibv_ctx.cmd_fd = cmd_fd;

	memset(&resp, 0, sizeof(resp));
	if (gethostname(context->hostname, sizeof(context->hostname)))
		strcpy(context->hostname, "host_unknown");

	tot_uuars = get_total_uuars(page_size);
	if (tot_uuars < 0) {
		errno = -tot_uuars;
		goto err_free;
	}

	low_lat_uuars = get_num_low_lat_uuars(tot_uuars);
	if (low_lat_uuars > tot_uuars - 1) {
		errno = ENOMEM;
		goto err_free;
	}

	memset(&req, 0, sizeof(req));

	req.total_num_uuars = tot_uuars;
	req.num_low_latency_uuars = low_lat_uuars;
	req.cqe_version = MLX5_CQE_VERSION_V1;
	req.lib_caps |= MLX5_LIB_CAP_4K_UAR;

	if (ibv_cmd_get_context(&context->ibv_ctx, &req.ibv_req, sizeof req,
				&resp.ibv_resp, sizeof resp))
		goto err_free;

	context->max_num_qps		= resp.qp_tab_size;
	context->bf_reg_size		= resp.bf_reg_size;
	context->tot_uuars		= resp.tot_uuars;
	context->low_lat_uuars		= low_lat_uuars;
	context->cache_line_size	= resp.cache_line_size;
	context->max_sq_desc_sz = resp.max_sq_desc_sz;
	context->max_rq_desc_sz = resp.max_rq_desc_sz;
	context->max_send_wqebb	= resp.max_send_wqebb;
	context->num_ports	= resp.num_ports;
	context->max_recv_wr	= resp.max_recv_wr;
	context->max_srq_recv_wr = resp.max_srq_recv_wr;
	adjust_uar_info(to_mdev(&vdev->device), context, resp);

	gross_uuars = context->tot_uuars / MLX5_NUM_NON_FP_BFREGS_PER_UAR * NUM_BFREGS_PER_UAR;
	context->bfs = calloc(gross_uuars, sizeof(*context->bfs));
	if (!context->bfs) {
		errno = ENOMEM;
		goto err_free;
	}
	context->cmds_supp_uhw = resp.cmds_supp_uhw;

	if (resp.eth_min_inline)
		context->eth_min_inline_size = ((resp.eth_min_inline - 1) == MLX5_INLINE_MODE_NONE) ?
					       0 : MLX5_ETH_INLINE_HEADER_SIZE;
	else
		context->eth_min_inline_size = MLX5_ETH_INLINE_HEADER_SIZE;

	if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_FLAGS) {
		context->compact_av   = resp.exp_data.flags & MLX5_CAP_COMPACT_AV;
		context->implicit_odp = resp.exp_data.flags & MLX5_CAP_ODP_IMPLICIT;
	}

	if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_CQE_COMP_MAX_NUM)
		context->cqe_comp_max_num = resp.exp_data.cqe_comp_max_num;

	if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_CQE_VERSION)
		context->cqe_version = resp.exp_data.cqe_version;

	if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_RROCE_UDP_SPORT_MIN)
		context->rroce_udp_sport_min = resp.exp_data.rroce_udp_sport_min;

	if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_RROCE_UDP_SPORT_MAX)
		context->rroce_udp_sport_max = resp.exp_data.rroce_udp_sport_max;

	if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_MAX_DESC_SZ_SQ_DC)
		context->max_desc_sz_sq_dc = resp.exp_data.max_desc_sz_sq_dc;

	if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_ATOMIC_SIZES_DC)
		context->atomic_sizes_dc = resp.exp_data.atomic_sizes_dc;

	ctx->ops = mlx5_ctx_ops;
	context->cqe_version = resp.cqe_version;
	if (context->cqe_version) {
		if (context->cqe_version == 1) {
			ctx->ops.poll_cq = mlx5_poll_cq_1;
		} else {
			printf("Unsupported cqe_vesion = %d, stay on  cqe version 0\n",
			       context->cqe_version);
			context->cqe_version = 0;
		}
	}

	attr.comp_mask = IBV_EXP_DEVICE_ATTR_RESERVED - 1;
	err = mlx5_exp_query_device(ctx, &attr);
	if (!err) {
		if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS)
			context->exp_device_cap_flags = attr.exp_device_cap_flags;

		if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN) {
			context->max_ctx_res_domain = attr.max_ctx_res_domain;
			mlx5_spinlock_init(&context->send_db_lock, !mlx5_single_threaded);
			INIT_LIST_HEAD(&context->send_wc_db_list);
			INIT_LIST_HEAD(&context->wc_uar_list);
		}
		if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_HCA_CORE_CLOCK_OFFSET) {
			context->core_clock.offset =
				resp.exp_data.hca_core_clock_offset &
				(to_mdev(ibdev)->page_size - 1);
			mlx5_map_internal_clock(to_mdev(ibdev), ctx);
			if (attr.hca_core_clock)
				context->core_clock.mult = ((1ull * 1000) << 21) /
					attr.hca_core_clock;
			else
				context->core_clock.mult = 0;

			/* ConnectX-4 supports 64bit timestamp. We choose these numbers
			 * in order to make sure that after arithmetic operations,
			 * we don't overflow a 64bit variable.
			 */
			context->core_clock.shift = 21;
			context->core_clock.mask = (1ULL << 49) - 1;
		}

		if (resp.exp_data.comp_mask & MLX5_EXP_ALLOC_CTX_RESP_MASK_CLOCK_INFO &&
		    resp.exp_data.clock_info_version_mask & 1)
			mlx5_map_clock_info(to_mdev(ibdev), ctx);

		if (attr.comp_mask & IBV_EXP_DEVICE_ATTR_MAX_DM_SIZE)
			context->max_dm_size = attr.max_dm_size;
	}

	pthread_mutex_init(&context->rsc_table_mutex, NULL);
	pthread_mutex_init(&context->srq_table_mutex, NULL);
	for (i = 0; i < MLX5_QP_TABLE_SIZE; ++i)
		context->rsc_table[i].refcnt = 0;

	for (i = 0; i < MLX5_QP_TABLE_SIZE; ++i)
		context->uidx_table[i].refcnt = 0;

	context->db_list = NULL;

	pthread_mutex_init(&context->db_list_mutex, NULL);

	context->prefer_bf = get_always_bf(&context->ibv_ctx);
	context->shut_up_bf = get_shut_up_bf(&context->ibv_ctx);
	context->enable_cqe_comp = get_cqe_comp(&context->ibv_ctx);
	mlx5_use_mutex = get_use_mutex(&context->ibv_ctx);

	offset = 0;
	set_command(MLX5_MMAP_MAP_DC_INFO_PAGE, &offset);
	context->cc.buf = mmap(NULL, 4096 * context->num_ports, PROT_READ,
			       MAP_PRIVATE, cmd_fd, page_size * offset);
	if (context->cc.buf == MAP_FAILED)
		context->cc.buf = NULL;

	mlx5_single_threaded = single_threaded_app(&context->ibv_ctx);

	num_sys_page_map = context->tot_uuars / (context->num_uars_per_page * MLX5_NUM_NON_FP_BFREGS_PER_UAR);
	for (i = 0; i < num_sys_page_map; ++i) {
		uar_mapped = 0;

		/* Don't map UAR to WC if BF is not used */
		if (!context->shut_up_bf) {
			context->uar[i].regs = mlx5_uar_mmap(i, MLX5_MMAP_GET_WC_PAGES_CMD, page_size, cmd_fd);
			if (context->uar[i].regs != MAP_FAILED) {
				context->uar[i].map_type = MLX5_UAR_MAP_WC;
				uar_mapped = 1;
			}
		}

		if (!uar_mapped) {
			context->uar[i].regs = mlx5_uar_mmap(i, MLX5_MMAP_GET_NC_PAGES_CMD, page_size, cmd_fd);
			if (context->uar[i].regs != MAP_FAILED) {
				context->uar[i].map_type = MLX5_UAR_MAP_NC;
				uar_mapped = 1;
			}
		}

		if (!uar_mapped) {
			/* for backward compatibility with old kernel driver */
			context->uar[i].regs = mlx5_uar_mmap(i, MLX5_MMAP_GET_REGULAR_PAGES_CMD, page_size, cmd_fd);
			if (context->uar[i].regs != MAP_FAILED) {
				context->uar[i].map_type = MLX5_UAR_MAP_WC;
				legacy_uar_map = 1;
				uar_mapped = 1;
			}
		}

		if (!uar_mapped) {
			context->uar[i].regs = NULL;
			goto err_free_cc;
		}
	}

	for (i = 0; i < num_sys_page_map; i++) {
		for (j = 0; j < context->num_uars_per_page; j++) {
			for (k = 0; k < NUM_BFREGS_PER_UAR; k++) {
				bfi = (i * context->num_uars_per_page + j) * NUM_BFREGS_PER_UAR + k;
				context->bfs[bfi].reg = context->uar[i].regs + MLX5_ADAPTER_PAGE_SIZE * j +
							MLX5_BF_OFFSET + k * context->bf_reg_size;
				context->bfs[bfi].need_lock = need_uuar_lock(context, bfi) &&
					    context->uar[i].map_type == MLX5_UAR_MAP_WC;
				mlx5_lock_init(&context->bfs[bfi].lock,
					       !mlx5_single_threaded,
					       mlx5_get_locktype());
				context->bfs[bfi].offset = 0;
				if (bfi)
					context->bfs[bfi].buf_size = context->bf_reg_size / 2;

				if (context->uar[i].map_type == MLX5_UAR_MAP_WC) {
					/* In case of legacy mapping we don't know if UAR
					 * mapped as NC or WC. To be on the safe side we assume
					 * mapping as WC but BF buf_size = 0 this will force DB
					 * on WC memory.
					 */
					context->bfs[bfi].buf_size = legacy_uar_map ? 0 : context->bf_reg_size / 2;
					context->bfs[bfi].db_method = (context->bfs[bfi].need_lock &&  !mlx5_single_threaded) ?
								       MLX5_DB_METHOD_BF :
								       (mlx5_single_threaded && wc_auto_evict_size() == 64 ?
								        MLX5_DB_METHOD_DEDIC_BF_1_THREAD :
								        MLX5_DB_METHOD_DEDIC_BF);
				} else {
					context->bfs[bfi].db_method = MLX5_DB_METHOD_DB;
				}

				context->bfs[bfi].uuarn = bfi;
			}
		}
	}

	mlx5_lock_init(&context->lock32,
		       !mlx5_single_threaded,
		       mlx5_get_locktype());

	mlx5_spinlock_init(&context->hugetlb_lock, !mlx5_single_threaded);
	INIT_LIST_HEAD(&context->hugetlb_list);

	pthread_mutex_init(&context->task_mutex, NULL);

	set_extended(verbs_ctx);
	set_experimental(ctx);

	for (i = 0; i < MLX5_MAX_PORTS_NUM; ++i)
		context->port_query_cache[i].valid = 0;

	return 0;

err_free_cc:
	if (context->cc.buf)
		munmap(context->cc.buf, 4096 * context->num_ports);

	if (context->clock_info_page)
		munmap(context->clock_info_page, to_mdev(ibdev)->page_size);

	if (context->hca_core_clock)
		munmap(context->hca_core_clock - context->core_clock.offset,
		       to_mdev(ibdev)->page_size);

	free(context->bfs);

err_free:
	for (i = 0; i < MLX5_MAX_UARS; ++i) {
		if (context->uar[i].regs)
			munmap(context->uar[i].regs, page_size);
	}
	close_debug_file(context);

	return errno;
}

static void mlx5_free_context(struct verbs_device *device,
			      struct ibv_context *ibctx)
{
	struct mlx5_context *context = to_mctx(ibctx);
	int page_size = to_mdev(ibctx->device)->page_size;
	int i;
	struct mlx5_wc_uar *wc_uar;

	if (context->clock_info_page)
		munmap(context->clock_info_page,
		       to_mdev(&device->device)->page_size);

	if (context->hca_core_clock)
		munmap(context->hca_core_clock - context->core_clock.offset,
		       to_mdev(&device->device)->page_size);

	if (context->cc.buf)
		munmap(context->cc.buf, 4096 * context->num_ports);

	free(context->bfs);
	for (i = 0; i < MLX5_MAX_UARS; ++i) {
		if (context->uar[i].regs)
			munmap(context->uar[i].regs, page_size);
	}

	if (context->max_ctx_res_domain) {
		mlx5_spin_lock(&context->send_db_lock);
		while (!list_empty(&context->wc_uar_list)) {
			wc_uar = list_entry(context->wc_uar_list.next,
					    struct mlx5_wc_uar, list);
			list_del(&wc_uar->list);
			free(wc_uar);
		}
		mlx5_spin_unlock(&context->send_db_lock);
	}

	close_debug_file(context);
}

static void mlx5_driver_uninit(struct verbs_device *verbs_device)
{
	struct mlx5_device *mlx5_dev = to_mdev(&verbs_device->device);

	free(mlx5_dev);
}

static struct verbs_device *mlx5_driver_init(const char *uverbs_sys_path,
					     int abi_version)
{
	char			value[8];
	struct mlx5_device     *dev;
	unsigned		vendor, device;
	int			i;

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/vendor",
				value, sizeof value) < 0)
		return NULL;
	sscanf(value, "%i", &vendor);

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/device",
				value, sizeof value) < 0)
		return NULL;
	sscanf(value, "%i", &device);

	for (i = 0; i < sizeof hca_table / sizeof hca_table[0]; ++i)
		if (vendor == hca_table[i].vendor &&
		    device == hca_table[i].device)
			goto found;

	return NULL;

found:
	if (abi_version < MLX5_UVERBS_MIN_ABI_VERSION ||
	    abi_version > MLX5_UVERBS_MAX_ABI_VERSION) {
		fprintf(stderr, PFX "Fatal: ABI version %d of %s is not supported "
			"(min supported %d, max supported %d)\n",
			abi_version, uverbs_sys_path,
			MLX5_UVERBS_MIN_ABI_VERSION,
			MLX5_UVERBS_MAX_ABI_VERSION);
		return NULL;
	}

	dev = malloc(sizeof *dev);
	if (!dev) {
		fprintf(stderr, PFX "Fatal: couldn't allocate device for %s\n",
			uverbs_sys_path);
		return NULL;
	}

	dev->page_size = sysconf(_SC_PAGESIZE);

	dev->devid.id = device;
	dev->driver_abi_ver = abi_version;

	dev->verbs_dev.sz = sizeof(dev->verbs_dev);
	dev->verbs_dev.size_of_context =
		sizeof(struct mlx5_context) - sizeof(struct ibv_context);

	/*
	 * mlx5_init_context will initialize provider calls
	 */
	dev->verbs_dev.init_context = mlx5_alloc_context;
	dev->verbs_dev.uninit_context = mlx5_free_context;
	dev->verbs_dev.verbs_uninit_func = mlx5_driver_uninit;

	return &dev->verbs_dev;
}

#ifdef HAVE_IBV_REGISTER_DRIVER
static __attribute__((constructor)) void mlx5_register_driver(void)
{
	verbs_register_driver("mlx5", mlx5_driver_init);
}
#else
/*
 * Export the old libsysfs sysfs_class_device-based driver entry point
 * if libibverbs does not export an ibv_register_driver() function.
 */
struct ibv_device *openib_driver_init(struct sysfs_class_device *sysdev)
{
	int abi_ver = 0;
	char value[8];

	if (ibv_read_sysfs_file(sysdev->path, "abi_version",
				value, sizeof value) > 0)
		abi_ver = strtol(value, NULL, 10);

	return mlx5_driver_init(sysdev->path, abi_ver);
}
#endif /* HAVE_IBV_REGISTER_DRIVER */
