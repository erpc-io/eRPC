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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sched.h>
#ifndef HAVE_IBV_REGISTER_DRIVER
#include <sysfs/libsysfs.h>
#endif

#include "mlx4.h"
#include "mlx4-abi.h"
#include "mlx4_exp.h"


#ifndef PCI_VENDOR_ID_MELLANOX
#define PCI_VENDOR_ID_MELLANOX			0x15b3
#endif

int mlx4_trace = 0;
int mlx4_single_threaded = 0;
int mlx4_use_mutex = 0;

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
	HCA(MELLANOX, 0x6340),	/* MT25408 "Hermon" SDR */
	HCA(MELLANOX, 0x634a),	/* MT25408 "Hermon" DDR */
	HCA(MELLANOX, 0x6354),	/* MT25408 "Hermon" QDR */
	HCA(MELLANOX, 0x6732),	/* MT25408 "Hermon" DDR PCIe gen2 */
	HCA(MELLANOX, 0x673c),	/* MT25408 "Hermon" QDR PCIe gen2 */
	HCA(MELLANOX, 0x6368),	/* MT25408 "Hermon" EN 10GigE */
	HCA(MELLANOX, 0x6750),	/* MT25408 "Hermon" EN 10GigE PCIe gen2 */
	HCA(MELLANOX, 0x6372),	/* MT25458 ConnectX EN 10GBASE-T 10GigE */
	HCA(MELLANOX, 0x675a),	/* MT25458 ConnectX EN 10GBASE-T+Gen2 10GigE */
	HCA(MELLANOX, 0x6764),	/* MT26468 ConnectX EN 10GigE PCIe gen2*/
	HCA(MELLANOX, 0x6746),	/* MT26438 ConnectX EN 40GigE PCIe gen2 5GT/s */
	HCA(MELLANOX, 0x676e),	/* MT26478 ConnectX2 40GigE PCIe gen2 */
	HCA(MELLANOX, 0x1002),	/* MT25400 Family [ConnectX-2 Virtual Function] */
	HCA(MELLANOX, 0x1003),	/* MT27500 Family [ConnectX-3] */
	HCA(MELLANOX, 0x1004),	/* MT27500 Family [ConnectX-3 Virtual Function] */
	HCA(MELLANOX, 0x1005),	/* MT27510 Family */
	HCA(MELLANOX, 0x1006),	/* MT27511 Family */
	HCA(MELLANOX, 0x1007),	/* MT27520 Family */
	HCA(MELLANOX, 0x1008),	/* MT27521 Family */
	HCA(MELLANOX, 0x1009),	/* MT27530 Family */
	HCA(MELLANOX, 0x100a),	/* MT27531 Family */
	HCA(MELLANOX, 0x100b),	/* MT27540 Family */
	HCA(MELLANOX, 0x100c),	/* MT27541 Family */
	HCA(MELLANOX, 0x100d),	/* MT27550 Family */
	HCA(MELLANOX, 0x100e),	/* MT27551 Family */
	HCA(MELLANOX, 0x100f),	/* MT27560 Family */
	HCA(MELLANOX, 0x1010),	/* MT27561 Family */
};

static struct ibv_context_ops mlx4_ctx_ops = {
	.query_device  = mlx4_query_device,
	.query_port    = mlx4_query_port,
	.alloc_pd      = mlx4_alloc_pd,
	.dealloc_pd    = mlx4_free_pd,
	.reg_mr	       = mlx4_reg_mr,
	.rereg_mr      = mlx4_rereg_mr,
	.dereg_mr      = mlx4_dereg_mr,
	.alloc_mw	   = mlx4_alloc_mw,
	.dealloc_mw    = mlx4_dealloc_mw,
	.bind_mw       = mlx4_bind_mw,
	.create_cq     = mlx4_create_cq,
	.poll_cq       = mlx4_poll_ibv_cq,
	.req_notify_cq = mlx4_arm_cq,
	.cq_event      = mlx4_cq_event,
	.resize_cq     = mlx4_resize_cq,
	.destroy_cq    = mlx4_destroy_cq,
	.create_srq    = mlx4_create_srq,
	.modify_srq    = mlx4_modify_srq,
	.query_srq     = mlx4_query_srq,
	.destroy_srq   = mlx4_destroy_srq,
	.post_srq_recv = mlx4_post_srq_recv,
	.create_qp     = mlx4_create_qp,
	.query_qp      = mlx4_query_qp,
	.modify_qp     = mlx4_modify_qp,
	.destroy_qp    = mlx4_destroy_qp,
	.post_send     = mlx4_post_send,
	.post_recv     = mlx4_post_recv,
	.create_ah     = mlx4_create_ah,
	.destroy_ah    = mlx4_destroy_ah,
	.attach_mcast  = ibv_cmd_attach_mcast,
	.detach_mcast  = ibv_cmd_detach_mcast
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

static int mlx4_is_sandy_bridge(int *num_cores)
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
		    (cur_cpu_model == 0x2A || cur_cpu_model == 0x2D))
			rc = 1;
	}

	fclose(fd);
	return rc;
}

static void mlx4_check_numa_enabled(struct ibv_context *context)
{
	char fname[MAXPATHLEN];
	char buf[128];
	FILE *fp;
	int numa_enabled;
	char env[VERBS_MAX_ENV_VAL];

	snprintf(fname, MAXPATHLEN, "/sys/class/infiniband/%s/device/numa_node",
		 ibv_get_device_name(context->device));

	fp = fopen(fname, "r");
	if (!fp) {
		fprintf(stderr, PFX "Warning: can not check if NUMA is enabled "
			"on node: failed to open %s\n", fname);
		return;
	}

	if (!fgets(buf, sizeof(buf), fp)) {
		fprintf(stderr, PFX "Warning: can not check if NUMA is enabled "
			"on node: failed to read numa node value\n");
		goto out;
	}

	numa_enabled = (strtol(buf, 0, 10) >= 0);
	if (numa_enabled)
		printf(PFX "Device NUMA node detection is supported\n");
	else if (ibv_exp_cmd_getenv(context, "MLX4_LOCAL_CPUS", env, sizeof(env)))
		printf(PFX "Warning: Device NUMA node detection is not supported. "
		       "Please consider setting the environment variable "
			"'MLX4_LOCAL_CPUS' or enable ACPI SLIT\n");
out:
	fclose(fp);
}

static void dump_cpu_set(cpu_set_t *cpu_set)
{
	int i;
	int first_cpu = -1;
	int last_cpu = -1;
	int n = 0;

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, cpu_set)) {
			if (first_cpu < 0)
				first_cpu = i;
			if (i == CPU_SETSIZE - 1)
				last_cpu = i;
		} else if (first_cpu >= 0)
			last_cpu = i - 1;

		if (last_cpu >= 0) {
			if (first_cpu != last_cpu)
				printf("%s%d-%d", n ? "," : "", first_cpu,
				       last_cpu);
			else
				printf("%s%d", n ? "," : "", last_cpu);

			first_cpu = -1;
			last_cpu = -1;
			++n;
		}
	}
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
static void mlx4_local_cpu_set(struct ibv_context *context, cpu_set_t *cpu_set)
{
	char *p, buf[1024];
	char env_value[VERBS_MAX_ENV_VAL];
	uint32_t word;
	int i, k;

	if (mlx4_trace)
		mlx4_check_numa_enabled(context);

	if (!ibv_exp_cmd_getenv(context, "MLX4_LOCAL_CPUS", env_value, sizeof(env_value))) {
		strncpy(buf, env_value, sizeof(buf));
		if (mlx4_trace)
			printf(PFX "Local CPUs flags were override by %s\n", buf);
	} else {
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

static int mlx4_enable_sandy_bridge_fix(struct ibv_context *context)
{
	cpu_set_t my_cpus, dev_local_cpus, result_set;
	int stall_enable;
	int ret;
	int num_cores;

	if (!mlx4_is_sandy_bridge(&num_cores))
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

	if (mlx4_trace) {
		printf(PFX "Running on cpus: ");
		dump_cpu_set(&my_cpus);
		printf("\n");
	}

	/* get device local cpu set */
	mlx4_local_cpu_set(context, &dev_local_cpus);

	/* make sure result_set is not init to all 0 */
	CPU_SET(0, &result_set);
	/* Set stall_enable if my cpu set and dev cpu set are disjoint sets */
	CPU_AND(&result_set, &my_cpus, &dev_local_cpus);
	stall_enable = CPU_COUNT(&result_set) ? 0 : 1;

	if (mlx4_trace) {
		printf(PFX "HCA:%s local cpus: ", ibv_get_device_name(context->device));
		dump_cpu_set(&dev_local_cpus);
		printf("\n");
		if (CPU_COUNT(&my_cpus) == num_cores) {
			printf(PFX "Warning: CPU affinity wasn't used for this "
				   "process, if the system has more than one numa node, it might be using a remote one.\n");
			printf(PFX "         For achieving better performance, "
				   "please consider setting the CPU "
				   "affinity.\n");
		}
	}

out:
	if (mlx4_trace)
		printf(PFX "Sandy Bridge CPU was detected, cq_stall is %s\n",
		       stall_enable ? "enabled" : "disabled");

	return stall_enable;
}

static void mlx4_read_env(struct ibv_device *ibdev, struct mlx4_context *ctx)
{
	char env_value[VERBS_MAX_ENV_VAL];

	if (!ibv_exp_cmd_getenv(&ctx->ibv_ctx, "MLX4_TRACE", env_value, sizeof(env_value)) &&
	    (strcmp(env_value, "0")))
		mlx4_trace = 1;

	if (!ibv_exp_cmd_getenv(&ctx->ibv_ctx, "MLX4_STALL_CQ_POLL", env_value, sizeof(env_value)) &&
	    !strcmp(env_value, "0"))
		/* check if cq stall is overrided by user */
		ctx->stall_enable = 0;
	else
		/* autodetect if we need to do cq polling */
		ctx->stall_enable = mlx4_enable_sandy_bridge_fix(&ctx->ibv_ctx);

	if (!ibv_exp_cmd_getenv(&ctx->ibv_ctx, "MLX4_STALL_NUM_LOOP", env_value, sizeof(env_value)))
		mlx4_stall_num_loop = atoi(env_value);

	if (!ibv_exp_cmd_getenv(&ctx->ibv_ctx, "MLX4_SINGLE_THREADED", env_value, sizeof(env_value)))
		mlx4_single_threaded = strcmp(env_value, "1") ? 0 : 1;

	if (!ibv_exp_cmd_getenv(&ctx->ibv_ctx,
				"MLX4_USE_MUTEX",
				env_value,
				sizeof(env_value)))
		mlx4_use_mutex = strcmp(env_value, "1") ? 0 : 1;
}

void read_init_vars(struct mlx4_context *ctx)
{
	char env_value[VERBS_MAX_ENV_VAL];

	pthread_mutex_lock(&ctx->env_mtx);
	if (!ctx->env_initialized) {
		mlx4_read_env(ctx->ibv_ctx.device, ctx);
		if (!ibv_exp_cmd_getenv(&ctx->ibv_ctx, "MLX4_POST_SEND_PREFER_BF", env_value, sizeof(env_value))) {
			ctx->prefer_bf = !!strcmp(env_value, "0");
			if (mlx4_trace)
				printf(PFX "prefer_bf=%d\n", ctx->prefer_bf);
		} else {
			ctx->prefer_bf = 1;
		}

		ctx->env_initialized = 1;
	}
	pthread_mutex_unlock(&ctx->env_mtx);
}

static int mlx4_init_context(struct verbs_device *v_device,
			     struct ibv_context *ibv_ctx, int cmd_fd)
{
	struct mlx4_context	        *context;
	struct mlx4_alloc_ucontext_req  req;
	struct mlx4_alloc_ucontext_resp resp;
	struct mlx4_alloc_ucontext_resp_v3 resp_v3;
	int				i;
	struct ibv_exp_device_attr	dev_attrs;
	struct ibv_device_attr	           dev_legacy_attrs;
	struct mlx4_device		*dev = to_mdev(&v_device->device);
	unsigned int			qp_tab_size;
	unsigned int			bf_reg_size;
	unsigned int			cqe_size;
	int				hca_clock_offset;
	void				*hca_clock_page = NULL;

	/* verbs_context should be used for new verbs.
	 * memory footprint of mlx4_context and verbs_context share
	 * struct ibv_context.
	 */
	struct verbs_context *verbs_ctx = verbs_get_ctx(ibv_ctx);
	struct verbs_context_exp *verbs_exp_ctx = verbs_get_exp_ctx(ibv_ctx);

	memset(&req, 0, sizeof(req));
	context = to_mctx(ibv_ctx);
	ibv_ctx->cmd_fd = cmd_fd;
	ibv_ctx->device = &v_device->device;

	if (pthread_mutex_init(&context->env_mtx, NULL))
		return EIO;

	if (dev->driver_abi_ver > 3) {
#ifdef MLX4_WQE_FORMAT
		req.lib_caps = MLX4_USER_DEV_CAP_WQE_FORMAT;
#endif
		if (ibv_cmd_get_context(ibv_ctx, &req.cmd, sizeof(req),
					&resp.ibv_resp, sizeof(resp)))
			return errno;

		VALGRIND_MAKE_MEM_DEFINED(&resp, sizeof(resp));
		qp_tab_size			= resp.qp_tab_size;
		bf_reg_size			= resp.bf_reg_size;
		context->bf_regs_per_page	= resp.bf_regs_per_page;
		cqe_size			= resp.cqe_size;
	} else {
		if (ibv_cmd_get_context(ibv_ctx, &req.cmd, sizeof(req.cmd),
					&resp_v3.ibv_resp, sizeof(resp_v3)))
			return errno;

		VALGRIND_MAKE_MEM_DEFINED(&resp_v3, sizeof(resp_v3));
		qp_tab_size			= resp_v3.qp_tab_size;
		bf_reg_size			= resp_v3.bf_reg_size;
		context->bf_regs_per_page	= resp_v3.bf_regs_per_page;
		cqe_size			= 32;
	}

	context->num_qps	= qp_tab_size;
	context->qp_table_shift = ffs(context->num_qps) - 1 - MLX4_QP_TABLE_BITS;
	context->qp_table_mask	= (1 << context->qp_table_shift) - 1;
	context->cqe_size = cqe_size;
	for (i = 0; i < MLX4_PORTS_NUM; ++i)
		context->port_query_cache[i].valid = 0;

	pthread_mutex_init(&context->qp_table_mutex, NULL);
	for (i = 0; i < MLX4_QP_TABLE_SIZE; ++i)
		context->qp_table[i].refcnt = 0;

	for (i = 0; i < MLX4_NUM_DB_TYPE; ++i)
		context->db_list[i] = NULL;

	mlx4_init_xsrq_table(&context->xsrq_table, qp_tab_size);
	pthread_mutex_init(&context->db_list_mutex, NULL);

	context->uar = mmap(NULL, dev->page_size, PROT_WRITE,
			    MAP_SHARED, cmd_fd, 0);
	if (context->uar == MAP_FAILED)
		return errno;

	if (bf_reg_size) {
		context->bfs.page = mmap(NULL, dev->page_size,
					 PROT_WRITE, MAP_SHARED, cmd_fd,
					 dev->page_size);
		if (context->bfs.page == MAP_FAILED) {
			fprintf(stderr, PFX "Warning: BlueFlame available, "
				"but failed to mmap() BlueFlame page.\n");
			context->bfs.page		= NULL;
			context->bfs.buf_size		= 0;
			context->bfs.num_dedic_bfs	= 0;
		} else {
			context->bfs.num_dedic_bfs = min(context->bf_regs_per_page - 1,
							 MLX4_MAX_BFS_IN_PAGE - 1);
			context->bfs.buf_size = bf_reg_size / 2;
			mlx4_spinlock_init(&context->bfs.dedic_bf_lock, !mlx4_single_threaded);
			context->bfs.cmn_bf.address = context->bfs.page;

			mlx4_lock_init(&context->bfs.cmn_bf.lock,
				       !mlx4_single_threaded,
				       mlx4_get_locktype());

			context->bfs.dedic_bf_free = context->bfs.num_dedic_bfs;
			for (i = 0; i < context->bfs.num_dedic_bfs; i++) {
				context->bfs.dedic_bf[i].address   = context->bfs.page + (i + 1) * MLX4_BFS_STRIDE;
				context->bfs.dedic_bf_used[i] = 0;
			}
		}
	} else {
		context->bfs.page		= NULL;
		context->bfs.buf_size		= 0;
		context->bfs.num_dedic_bfs	= 0;
	}

	mlx4_spinlock_init(&context->uar_lock, !mlx4_single_threaded);

	mlx4_spinlock_init(&context->send_db_lock, !mlx4_single_threaded);
	INIT_LIST_HEAD(&context->send_db_list);

	mlx4_spinlock_init(&context->hugetlb_lock, !mlx4_single_threaded);
	INIT_LIST_HEAD(&context->hugetlb_list);

	pthread_mutex_init(&context->task_mutex, NULL);

	memset(&dev_attrs, 0, sizeof(dev_attrs));
	dev_attrs.comp_mask = IBV_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK |
			      IBV_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK |
			      IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS |
			      IBV_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN;

	if (mlx4_exp_query_device(ibv_ctx, &dev_attrs)) {
		if (mlx4_query_device(ibv_ctx, &dev_legacy_attrs))
			goto query_free;

		memcpy(&dev_attrs, &dev_legacy_attrs, sizeof(dev_legacy_attrs));
	}

	context->max_qp_wr = dev_attrs.max_qp_wr;
	context->max_sge = dev_attrs.max_sge;
	context->max_cqe = dev_attrs.max_cqe;
	context->exp_device_cap_flags = dev_attrs.exp_device_cap_flags;
	if (dev_attrs.comp_mask & IBV_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN)
		context->max_ctx_res_domain = dev_attrs.max_ctx_res_domain;

	VALGRIND_MAKE_MEM_DEFINED(&context->hca_core_clock, sizeof(context->hca_core_clock));
	if (dev_attrs.comp_mask & IBV_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK) {
		if (dev_attrs.hca_core_clock)
			context->core_clk.mult = ((1ull * 1000) << 29) /
						dev_attrs.hca_core_clock;
		else
			context->core_clk.mult = 0;

		context->core_clk.shift = 29;
		context->core_clk.mask = dev_attrs.timestamp_mask;

		if (ioctl(cmd_fd, MLX4_IOCHWCLOCKOFFSET,
			  &hca_clock_offset) >= 0) {
			VALGRIND_MAKE_MEM_DEFINED(&hca_clock_offset, sizeof(hca_clock_offset));
			context->core_clk.offset = hca_clock_offset;
			hca_clock_page = mmap(NULL, hca_clock_offset +
					sizeof(context->core_clk.mask),
					PROT_READ, MAP_SHARED, cmd_fd,
					dev->page_size *
					(MLX4_IB_MMAP_GET_HW_CLOCK));

			if (hca_clock_page == MAP_FAILED) {
				fprintf(stderr, PFX
					"Warning: Timestamp available,\n"
					"but failed to mmap() hca core  "
					"clock page.\n");
			} else {
				context->hca_core_clock = hca_clock_page +
					context->core_clk.offset;
			}
		}
	}

	ibv_ctx->ops = mlx4_ctx_ops;

	verbs_ctx->has_comp_mask |= VERBS_CONTEXT_XRCD | VERBS_CONTEXT_SRQ |
				    VERBS_CONTEXT_QP;

	verbs_set_ctx_op(verbs_ctx, close_xrcd, mlx4_close_xrcd);
	verbs_set_ctx_op(verbs_ctx, open_xrcd, mlx4_open_xrcd);
	verbs_set_ctx_op(verbs_ctx, create_srq_ex, mlx4_create_srq_ex);
	verbs_set_ctx_op(verbs_ctx, get_srq_num, verbs_get_srq_num);
	verbs_set_ctx_op(verbs_ctx, create_qp_ex, mlx4_create_qp_ex);
	verbs_set_ctx_op(verbs_ctx, open_qp, mlx4_open_qp);
	verbs_set_ctx_op(verbs_ctx, create_flow, ibv_cmd_create_flow);
	verbs_set_ctx_op(verbs_ctx, destroy_flow, ibv_cmd_destroy_flow);

	/*
	 * Set experimental verbs
	 */
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_reg_shared_mr, mlx4_reg_shared_mr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_create_flow, ibv_exp_cmd_create_flow);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_destroy_flow, ibv_exp_cmd_destroy_flow);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_create_ah, mlx4_exp_create_ah);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_query_device, mlx4_exp_query_device);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_create_qp, mlx4_exp_create_qp);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_modify_qp, mlx4_exp_modify_qp);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_query_port, mlx4_exp_query_port);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_modify_cq, mlx4_modify_cq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_post_task, mlx4_post_task);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_set_legacy_xrc, mlx4_set_legacy_xrc);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_get_legacy_xrc, mlx4_get_legacy_xrc);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_ibv_poll_cq, mlx4_exp_poll_cq);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_create_cq, mlx4_create_cq_ex);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_query_values, mlx4_query_values);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_reg_mr, mlx4_exp_reg_mr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_post_send, mlx4_exp_post_send);
	verbs_set_exp_ctx_op(verbs_exp_ctx, drv_exp_dereg_mr, mlx4_exp_dereg_mr);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_create_res_domain, mlx4_exp_create_res_domain);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_destroy_res_domain, mlx4_exp_destroy_res_domain);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_query_intf, mlx4_exp_query_intf);
	verbs_set_exp_ctx_op(verbs_exp_ctx, exp_release_intf, mlx4_exp_release_intf);

	return 0;

query_free:
	munmap(context->uar, dev->page_size);
	if (context->bfs.page)
		munmap(context->bfs.page, dev->page_size);
	if (hca_clock_page)
		munmap(hca_clock_page, hca_clock_offset +
		       sizeof(context->core_clk.mask));

	return errno;
}

static void mlx4_uninit_context(struct verbs_device *v_device,
				struct ibv_context *ibv_ctx)
{
	struct mlx4_context *context = to_mctx(ibv_ctx);

	munmap(context->uar, to_mdev(&v_device->device)->page_size);
	if (context->bfs.page)
		munmap(context->bfs.page,
		       to_mdev(&v_device->device)->page_size);
	if (context->hca_core_clock)
		munmap((context->hca_core_clock - context->core_clk.offset),
		       context->core_clk.offset + sizeof(context->core_clk.mask));
}

static void mlx4_driver_uninit(struct verbs_device *verbs_device)
{
	struct mlx4_device *mlx4_dev = to_mdev(&verbs_device->device);

	free(mlx4_dev);
}

static struct verbs_device *mlx4_driver_init(const char *uverbs_sys_path,
					     int abi_version)
{
	char			value[8];
	struct mlx4_device	*dev;
	unsigned		vendor, device;
	int			i;

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/vendor",
				value, sizeof value) < 0)
		return NULL;
	vendor = strtol(value, NULL, 16);

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/device",
				value, sizeof value) < 0)
		return NULL;
	device = strtol(value, NULL, 16);

	for (i = 0; i < sizeof hca_table / sizeof hca_table[0]; ++i)
		if (vendor == hca_table[i].vendor &&
		    device == hca_table[i].device)
			goto found;

	return NULL;

found:
	if (abi_version < MLX4_UVERBS_MIN_ABI_VERSION ||
	    abi_version > MLX4_UVERBS_MAX_ABI_VERSION) {
		fprintf(stderr, PFX "Fatal: ABI version %d of %s is not supported "
			"(min supported %d, max supported %d)\n",
			abi_version, uverbs_sys_path,
			MLX4_UVERBS_MIN_ABI_VERSION,
			MLX4_UVERBS_MAX_ABI_VERSION);
		return NULL;
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev) {
		fprintf(stderr, PFX "Fatal: couldn't allocate device for %s\n",
			uverbs_sys_path);
		return NULL;
	}

	dev->page_size   = sysconf(_SC_PAGESIZE);

	dev->devid.id = device;
	dev->driver_abi_ver = abi_version;

	dev->verbs_dev.sz = sizeof(*dev);
	dev->verbs_dev.size_of_context =
		sizeof(struct mlx4_context) - sizeof(struct ibv_context);
	/* mlx4_init_context will initialize provider calls */
	dev->verbs_dev.init_context = mlx4_init_context;
	dev->verbs_dev.uninit_context = mlx4_uninit_context;
	dev->verbs_dev.verbs_uninit_func = mlx4_driver_uninit;

	return &dev->verbs_dev;
}

#ifdef HAVE_IBV_REGISTER_DRIVER
static __attribute__((constructor)) void mlx4_register_driver(void)
{
	verbs_register_driver("mlx4", mlx4_driver_init);
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

	return mlx4_driver_init(sysdev->path, abi_ver);
}
#endif /* HAVE_IBV_REGISTER_DRIVER */
