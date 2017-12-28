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

#ifndef MLX5_H
#define MLX5_H

#include <stddef.h>
#include <stdio.h>
#include <netinet/in.h>

#include <infiniband/driver.h>
#include <infiniband/driver_exp.h>
#include <infiniband/verbs_exp.h>
#include <infiniband/peer_ops.h>
#include <infiniband/arch.h>
#include "mlx5dv.h"
#include "mlx5-abi.h"
#include "list.h"
#include "bitmap.h"
#include "implicit_lkey.h"
#include "wqe.h"

#ifdef __GNUC__
#define likely(x)	__builtin_expect((x), 1)
#define unlikely(x)	__builtin_expect((x), 0)
#endif

#ifndef uninitialized_var
#define uninitialized_var(x) x = x
#endif

#ifdef HAVE_VALGRIND_MEMCHECK_H

#  include <valgrind/memcheck.h>

#  if !defined(VALGRIND_MAKE_MEM_DEFINED) || !defined(VALGRIND_MAKE_MEM_UNDEFINED)
#    warning "Valgrind support requested, but VALGRIND_MAKE_MEM_(UN)DEFINED not available"
#  endif

#endif /* HAVE_VALGRIND_MEMCHECK_H */

#ifndef VALGRIND_MAKE_MEM_DEFINED
#  define VALGRIND_MAKE_MEM_DEFINED(addr, len)
#endif

#ifndef VALGRIND_MAKE_MEM_UNDEFINED
#  define VALGRIND_MAKE_MEM_UNDEFINED(addr, len)
#endif

#ifndef rmb
#  define rmb() mb()
#endif

#ifndef wmb
#  define wmb() mb()
#endif

#ifndef wc_wmb

#if defined(__i386__)
#define wc_wmb() asm volatile("lock; addl $0, 0(%%esp) " ::: "memory")
#elif defined(__x86_64__)
#define wc_wmb() asm volatile("sfence" ::: "memory")
#elif defined(__ia64__)
#define wc_wmb() asm volatile("fwb" ::: "memory")
#else
#define wc_wmb() wmb()
#endif

#endif

#define MLX5_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

#define MLX5_D_F_ALGN_SIZE (64)
#if MLX5_GCC_VERSION >= 403
#	define __MLX5_ALGN_F__ __attribute__((noinline, aligned(MLX5_D_F_ALGN_SIZE)))
#	define __MLX5_ALGN_D__ __attribute__((aligned(MLX5_D_F_ALGN_SIZE)))
#else
#	define __MLX5_ALGN_F__
#	define __MLX5_ALGN_D__
#endif

#ifndef min
#define min(a, b) \
	({ typeof(a) _a = (a); \
	   typeof(b) _b = (b); \
	   _a < _b ? _a : _b; })
#endif

#ifndef max
#define max(a, b) \
	({ typeof(a) _a = (a); \
	   typeof(b) _b = (b); \
	   _a > _b ? _a : _b; })
#endif

#define HIDDEN		__attribute__((visibility("hidden")))

#define PFX		"mlx5: "

#define MLX5_MAX_PORTS_NUM	2

enum {
	MLX5_MAX_CQ_FAMILY_VER		= 1,
	MLX5_MAX_QP_BURST_FAMILY_VER	= 1,
	MLX5_MAX_WQ_FAMILY_VER		= 0
};

enum {
	MLX5_IB_MMAP_CMD_SHIFT	= 8,
	MLX5_IB_MMAP_CMD_MASK	= 0xff,
};

enum {
	MLX5_QP_PATTERN = 0x012389AB,
	MLX5_CQ_PATTERN = 0x4567CDEF,
	MLX5_WQ_PATTERN = 0x89AB0123
};

enum mlx5_lock_type {
	MLX5_SPIN_LOCK = 0,
	MLX5_MUTEX = 1,
};

enum mlx5_lock_state {
	MLX5_USE_LOCK,
	MLX5_LOCKED,
	MLX5_UNLOCKED
};

enum {
	MLX5_MMAP_GET_REGULAR_PAGES_CMD    = 0,
	MLX5_MMAP_GET_CONTIGUOUS_PAGES_CMD = 1,
	MLX5_MMAP_GET_WC_PAGES_CMD	   = 2,
	MLX5_MMAP_GET_NC_PAGES_CMD	   = 3,
	MLX5_MMAP_MAP_DC_INFO_PAGE	   = 4,

	/* Use EXP mmap commands until it is pushed to upstream */
	MLX5_EXP_MMAP_GET_CORE_CLOCK_CMD		= 0xFB,
	MLX5_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA_CMD = 0xFC,
	MLX5_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA_CMD = 0xFD,
	MLX5_EXP_IB_MMAP_N_ALLOC_WC_CMD			= 0xFE,
	MLX5_EXP_IB_MMAP_CLOCK_INFO_CMD			= 0xFF,
};

enum {
	MLX5_EXP_CLOCK_INFO_V1	= 1,
};

enum {
	MLX5_CQE_VERSION_V0	= 0,
	MLX5_CQE_VERSION_V1	= 1,
};

enum {
	MLX5_ADAPTER_PAGE_SIZE		= 4096,
};

#define MLX5_CQ_PREFIX "MLX_CQ"
#define MLX5_QP_PREFIX "MLX_QP"
#define MLX5_MR_PREFIX "MLX_MR"
#define MLX5_RWQ_PREFIX "MLX_RWQ"
#define MLX5_MAX_LOG2_CONTIG_BLOCK_SIZE 23
#define MLX5_MIN_LOG2_CONTIG_BLOCK_SIZE 12

#define MLX5_DM_ALLOWED_ACCESS	(IBV_EXP_ACCESS_LOCAL_WRITE  |\
				 IBV_EXP_ACCESS_REMOTE_WRITE |\
				 IBV_EXP_ACCESS_REMOTE_READ  |\
				 IBV_EXP_ACCESS_REMOTE_ATOMIC)

enum {
	MLX5_DBG_QP		= 1 << 0,
	MLX5_DBG_CQ		= 1 << 1,
	MLX5_DBG_QP_SEND	= 1 << 2,
	MLX5_DBG_QP_SEND_ERR	= 1 << 3,
	MLX5_DBG_CQ_CQE		= 1 << 4,
	MLX5_DBG_CONTIG		= 1 << 5,
	MLX5_DBG_SRQ		= 1 << 6,
	MLX5_DBG_GEN		= 1 << 7,
};

enum {
	MLX5_UMR_PTR_ALIGN	= 2048,
};

extern uint32_t mlx5_debug_mask;
extern int mlx5_freeze_on_error_cqe;

#ifdef MLX5_DEBUG
#define mlx5_dbg(fp, mask, format, arg...)				\
do {									\
	if (mask & mlx5_debug_mask)					\
		fprintf(fp, "%s:%d: " format, __func__, __LINE__, ##arg);	\
} while (0)

#else
	#define mlx5_dbg(fp, mask, format, arg...)
#endif

enum {
	MLX5_STAT_RATE_OFFSET		= 5
};

enum {
	MLX5_QP_TABLE_SHIFT		= 12,
	MLX5_QP_TABLE_MASK		= (1 << MLX5_QP_TABLE_SHIFT) - 1,
	MLX5_QP_TABLE_SIZE		= 1 << (24 - MLX5_QP_TABLE_SHIFT),
};

enum {
	MLX5_SRQ_TABLE_SHIFT		= 12,
	MLX5_SRQ_TABLE_MASK		= (1 << MLX5_SRQ_TABLE_SHIFT) - 1,
	MLX5_SRQ_TABLE_SIZE		= 1 << (24 - MLX5_SRQ_TABLE_SHIFT),
};

enum {
	MLX5_DCT_TABLE_SHIFT		= 12,
	MLX5_DCT_TABLE_MASK		= (1 << MLX5_DCT_TABLE_SHIFT) - 1,
	MLX5_DCT_TABLE_SIZE		= 1 << (24 - MLX5_DCT_TABLE_SHIFT),
};

enum {
	MLX5_BF_OFFSET	= 0x800
};

enum {
	MLX5_OPC_MOD_MPW		= 0x01, /* OPC_MOD for LSO_MPW opcode */

	MLX5_OPCODE_SEND_ENABLE		= 0x17,
	MLX5_OPCODE_RECV_ENABLE		= 0x16,
	MLX5_OPCODE_CQE_WAIT		= 0x0f,
	MLX5_OPCODE_TAG_MATCHING	= 0x28,

	MLX5_RECV_OPCODE_RDMA_WRITE_IMM	= 0x00,
	MLX5_RECV_OPCODE_SEND		= 0x01,
	MLX5_RECV_OPCODE_SEND_IMM	= 0x02,
	MLX5_RECV_OPCODE_SEND_INVAL	= 0x03,

	MLX5_CQE_OPCODE_ERROR		= 0x1e,
	MLX5_CQE_OPCODE_RESIZE		= 0x16,
};

enum {
	MLX5_SRQ_FLAG_SIGNATURE		= (1 << 0),
	MLX5_SRQ_FLAG_TM_SW_CNT		= (1 << 6),
	MLX5_SRQ_FLAG_TM_CQE_REQ	= (1 << 7),
};

enum mlx5_alloc_type {
	MLX5_ALLOC_TYPE_ANON,
	MLX5_ALLOC_TYPE_HUGE,
	MLX5_ALLOC_TYPE_CONTIG,
	MLX5_ALLOC_TYPE_PEER_DIRECT,
	MLX5_ALLOC_TYPE_PREFER_HUGE,
	MLX5_ALLOC_TYPE_PREFER_CONTIG,
	MLX5_ALLOC_TYPE_ALL
};

enum mlx5_mr_type {
	MLX5_NORMAL_MR	= 0x0,
	MLX5_ODP_MR	= 0x1,
	MLX5_DM_MR	= 0x2,
};

enum {
	MLX5_UMR_CTRL_INLINE	= 1 << 7,
};

struct mlx5_device {
	struct verbs_device		verbs_dev;
	int				page_size;

	struct {
		unsigned id;
		unsigned short rev;
	} devid;
	int	driver_abi_ver;
};

enum mlx5_rsc_type {
	MLX5_RSC_TYPE_QP,
	MLX5_RSC_TYPE_DCT,
	MLX5_RSC_TYPE_RWQ,
	MLX5_RSC_TYPE_MP_RWQ,
	MLX5_RSC_TYPE_XSRQ,
	MLX5_RSC_TYPE_SRQ,
	MLX5_RSC_TYPE_INVAL,
};

enum {
	MLX5_USER_CMDS_SUPP_UHW_CREATE_AH    = 1 << 1,
};

struct mlx5_resource {
	enum mlx5_rsc_type	type;
	uint32_t		rsn;
};

struct mlx5_db_page;

struct mlx5_lock {
	pthread_mutex_t                 mutex;
	pthread_spinlock_t              slock;
	enum mlx5_lock_state            state;
	enum mlx5_lock_type             type;
};

struct mlx5_spinlock {
	pthread_spinlock_t		lock;
	enum mlx5_lock_state		state;
};

struct mlx5_atomic_info {
	int			valid;
	enum ibv_exp_atomic_cap	exp_atomic_cap;
	uint64_t	bit_mask_log_atomic_arg_sizes;
	uint64_t	masked_log_atomic_arg_sizes_network_endianness;
};

enum mlx5_uar_mapping_type {
	MLX5_UAR_MAP_WC,
	MLX5_UAR_MAP_NC
};
struct mlx5_uar_data {
	enum mlx5_uar_mapping_type	map_type;
	void				*regs;
};

struct mlx5_port_info_ctx {
	unsigned	consumer;
	int		steady;
};

struct mlx5_info_ctx {
	void				*buf;
	struct mlx5_port_info_ctx	port[2];
};

struct mlx5_context {
	struct ibv_context		ibv_ctx;
	int				max_num_qps;
	int				bf_reg_size;
	int				tot_uuars;
	int				low_lat_uuars;
	int				num_uars_per_page;
	int				bf_regs_per_page;
	int				num_bf_regs;
	int				prefer_bf;
	int				shut_up_bf;
	int				enable_cqe_comp;
	struct {
		struct mlx5_resource  **table;
		int			refcnt;
	}				rsc_table[MLX5_QP_TABLE_SIZE];
	pthread_mutex_t			rsc_table_mutex;

	struct {
		struct mlx5_srq	      **table;
		int			refcnt;
	}				srq_table[MLX5_SRQ_TABLE_SIZE];
	pthread_mutex_t			srq_table_mutex;

	struct {
		struct mlx5_resource  **table;
		int                     refcnt;
	}				uidx_table[MLX5_QP_TABLE_SIZE];
	pthread_mutex_t                 uidx_table_mutex;

	struct mlx5_uar_data		uar[MLX5_MAX_UARS];

	struct mlx5_spinlock		send_db_lock; /* protects send_db_list, wc_uar_list and send_db_num_uars */
	struct list_head		send_wc_db_list;
	struct list_head		wc_uar_list;
	unsigned int			num_wc_uars;
	int				max_ctx_res_domain;
	uint64_t			exp_device_cap_flags; /* Cached from device caps */

	struct mlx5_lock		lock32;
	struct mlx5_db_page	       *db_list;
	pthread_mutex_t			db_list_mutex;
	int				cache_line_size;
	int				max_sq_desc_sz;
	int				max_rq_desc_sz;
	int				max_send_wqebb;
	int				max_recv_wr;
	unsigned			max_srq_recv_wr;
	int				num_ports;
	int				stall_enable;
	int				stall_adaptive_enable;
	int				stall_cycles;
	struct mlx5_bf		       *bfs;
	FILE			       *dbg_fp;
	char				hostname[40];
	struct mlx5_spinlock            hugetlb_lock;
	struct list_head                hugetlb_list;
	int				max_desc_sz_sq_dc;
	uint32_t			atomic_sizes_dc;
	pthread_mutex_t                 task_mutex;
	struct mlx5_atomic_info		info;
	int				max_sge;
	uint32_t			max_send_wqe_inline_klms;
	pthread_mutex_t			env_mtx;
	int				env_initialized;
	int				compact_av;
	int				implicit_odp;
	int				numa_id;
	struct mlx5_info_ctx		cc;
	uint8_t                         cqe_version;
	uint16_t			cqe_comp_max_num;
	uint16_t			rroce_udp_sport_min;
	uint16_t			rroce_udp_sport_max;
	struct {
		uint8_t			valid;
		uint8_t			link_layer;
		enum ibv_port_cap_flags	caps;
	} port_query_cache[MLX5_MAX_PORTS_NUM];
	struct {
		uint64_t                offset;
		uint64_t                mask;
		uint32_t		mult;
		uint8_t			shift;
	} core_clock;
	void			       *hca_core_clock;
	void			       *clock_info_page;
	uint32_t			max_tso;
	int				cmds_supp_uhw;
	uint32_t			uar_size;
	uint32_t			eth_min_inline_size;
	uint64_t			max_dm_size;
};

struct mlx5_bitmap {
	uint32_t		last;
	uint32_t		top;
	uint32_t		max;
	uint32_t		avail;
	uint32_t		mask;
	unsigned long	       *table;
};

struct mlx5_hugetlb_mem {
	int			shmid;
	void		       *shmaddr;
	struct mlx5_bitmap	bitmap;
	struct list_head	list;
};

struct mlx5_numa_req {
	int valid;
	int numa_id;
};

struct mlx5_peer_direct_mem {
	uint32_t dir;
	uint64_t va_id;
	struct ibv_exp_peer_buf *pb;
	struct ibv_exp_peer_direct_attr *ctx;
};

struct mlx5_buf {
	void			       *buf;
	size_t				length;
	int                             base;
	struct mlx5_hugetlb_mem	       *hmem;
	struct mlx5_peer_direct_mem     peer;
	enum mlx5_alloc_type		type;
	struct mlx5_numa_req		numa_req;
	int				numa_alloc;
};

struct mlx5_pd {
	struct ibv_pd			ibv_pd;
	uint32_t			pdn;
	struct mlx5_implicit_lkey       r_ilkey;
	struct mlx5_implicit_lkey       w_ilkey;
	struct mlx5_implicit_lkey      *remote_ilkey;
};

enum {
	MLX5_CQ_SET_CI	= 0,
	MLX5_CQ_ARM_DB	= 1,
};

enum mlx5_cq_model_flags {
	/*
	 * When set the CQ API must be thread safe.
	 * When reset application is taking care
	 * to sync between CQ API calls.
	 */
	MLX5_CQ_MODEL_FLAG_THREAD_SAFE = 1 << 0,
	MLX5_CQ_MODEL_FLAG_DV_OWNED = 1 << 1,
};

enum mlx5_cq_creation_flags {
	/* When set, CQ supports timestamping */
	MLX5_CQ_CREATION_FLAG_COMPLETION_TIMESTAMP = 1 << 0,
	MLX5_CQ_CREATION_FLAG_COMPRESSED_CQE	   = 1 << 1,
};

struct mlx5_mini_cqe8 {
	union {
		uint32_t rx_hash_result;
		uint32_t checksum;
		struct {
			uint16_t wqe_counter;
			uint8_t  s_wqe_opcode;
			uint8_t  reserved;
		} s_wqe_info;
	};
	uint32_t byte_cnt;
};

enum {
	MLX5_MINI_ARR_SIZE	= 8
};

struct mlx5_peek_entry {
	uint32_t busy;
	uint32_t next;
};

struct mlx5_cq {
	struct ibv_cq			ibv_cq;
	uint32_t			creation_flags;
	uint32_t			pattern;
	struct mlx5_buf			buf_a;
	struct mlx5_buf			buf_b;
	struct mlx5_buf		       *active_buf;
	struct mlx5_buf		       *resize_buf;
	int				resize_cqes;
	int				active_cqes;
	struct mlx5_lock		lock;
	uint32_t			cqn;
	uint32_t			cons_index;
	uint32_t                        wait_index;
	uint32_t                        wait_count;
	volatile uint32_t	       *dbrec;
	int				arm_sn;
	int				cqe_sz;
	int				resize_cqe_sz;
	int				stall_next_poll;
	int				stall_enable;
	uint64_t			stall_last_count;
	int				stall_adaptive_enable;
	int				stall_cycles;
	uint8_t				model_flags; /* use mlx5_cq_model_flags */
	uint16_t			cqe_comp_max_num;
	uint8_t				cq_log_size;
	/* Compressed CQE data */
	struct mlx5_cqe64		next_decomp_cqe64;
	struct mlx5_resource	       *compressed_rsc;
	uint16_t			compressed_left;
	uint16_t			compressed_wqe_cnt;
	uint8_t				compressed_req;
	uint8_t				compressed_mp_rq;
	uint8_t				mini_arr_idx;
	struct mlx5_mini_cqe8		mini_array[MLX5_MINI_ARR_SIZE];
	/* peer-direct data */
	int					peer_enabled;
	struct ibv_exp_peer_direct_attr	       *peer_ctx;
	struct mlx5_buf				peer_buf;
	struct mlx5_peek_entry		      **peer_peek_table;
	struct mlx5_peek_entry		       *peer_peek_free;
};

struct mlx5_tag_entry {
	struct mlx5_tag_entry *next;
	uint64_t	       wr_id;
	int		       phase_cnt;
	void		      *ptr;
	uint32_t	       size;
	int8_t		       expect_cqe;
};

struct mlx5_srq_op {
	struct mlx5_tag_entry *tag;
	uint64_t	       wr_id;
	uint32_t	       wqe_head;
};

struct mlx5_srq {
	struct mlx5_resource            rsc;  /* This struct must be first */
	struct verbs_srq		vsrq;
	struct mlx5_buf			buf;
	struct mlx5_spinlock		lock;
	uint64_t		       *wrid;
	uint32_t			srqn;
	int				max;
	int				max_gs;
	int				wqe_shift;
	int				head;
	int				tail;
	volatile uint32_t	       *db;
	uint16_t			counter;
	int				wq_sig;
	struct ibv_srq_legacy *ibv_srq_legacy;
	int				is_xsrq;
	struct mlx5_qp		       *cmd_qp;
	struct mlx5_tag_entry	       *tm_list; /* vector of all tags */
	struct mlx5_tag_entry	       *tm_head; /* queue of free tags */
	struct mlx5_tag_entry	       *tm_tail;
	struct mlx5_srq_op	       *op;
	int				op_head;
	int				op_tail;
	int				unexp_in;
	int				unexp_out;
};

static inline void mlx5_tm_release_tag(struct mlx5_srq *srq,
				       struct mlx5_tag_entry *tag)
{
	if (!--tag->expect_cqe) {
		tag->next = NULL;
		srq->tm_tail->next = tag;
		srq->tm_tail = tag;
	}
}

struct wr_list {
	uint16_t	opcode;
	uint16_t	next;
};

struct mlx5_wq {
	/* common hot data */
	uint64_t		       *wrid;
	unsigned			wqe_cnt;
	unsigned			head;
	unsigned			tail;
	unsigned			max_post;
	int				max_gs;
	struct mlx5_lock		lock;
	/* post_recv hot data */
	void			       *buff;
	volatile uint32_t	       *db;
	int				wqe_shift;
	int				offset;
	uint32_t			*wr_data;
};

struct mlx5_wq_recv_send_enable {
	unsigned			head_en_index;
	unsigned			head_en_count;
};

enum mlx5_db_method {
	MLX5_DB_METHOD_DEDIC_BF_1_THREAD,
	MLX5_DB_METHOD_DEDIC_BF,
	MLX5_DB_METHOD_BF,
	MLX5_DB_METHOD_DB
};

struct mlx5_bf {
	void			       *reg;
	int				need_lock;
	/*
	 * Protect usage of BF address field including data written to the BF
	 * and the BF buffer toggling.
	 */
	struct mlx5_lock		lock;
	unsigned			offset;
	unsigned			buf_size;
	unsigned			uuarn;
	enum mlx5_db_method		db_method;
};

struct mlx5_dm {
	struct ibv_exp_dm		ibdm;
	size_t				length;
	void			       *start_va;
};

struct mlx5_mr {
	struct ibv_mr			ibv_mr;
	struct mlx5_buf			buf;
	uint64_t			alloc_flags;
	enum mlx5_mr_type		type;
};

enum mlx5_qp_model_flags {
	/*
	 * When set the QP API must be thread safe.
	 * When reset application is taking care
	 * to sync between QP API calls.
	 */
	MLX5_QP_MODEL_FLAG_THREAD_SAFE = 1 << 0,
	MLX5_QP_MODEL_MULTI_PACKET_WQE = 1 << 1,
	MLX5_QP_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP = 1 << 2,
};

enum {
	CREATE_FLAG_NO_DOORBELL = IBV_EXP_QP_CREATE_INTERNAL_USE
};

enum {
	MLX5_QP_PEER_VA_ID_DBR = 0,
	MLX5_QP_PEER_VA_ID_BF  = 1,
	MLX5_QP_PEER_VA_ID_MAX = 2
};

struct mlx5_qp;
struct general_data_hot {
	/* post_send hot data */
	unsigned		*wqe_head;
	int			(*post_send_one)(struct ibv_exp_send_wr *wr,
						 struct mlx5_qp *qp,
						 uint64_t exp_send_flags,
						 void *seg, int *total_size);
	void			*sqstart;
	void			*sqend;
	volatile uint32_t	*db;
	struct mlx5_bf		*bf;
	uint32_t		 scur_post;
	/* Used for burst_family interface, keeps the last posted wqe */
	uint32_t		 last_post;
	uint16_t		 create_flags;
	uint8_t			 fm_cache;
	uint8_t			 model_flags; /* use mlx5_qp_model_flags */
};
enum mpw_states {
	MLX5_MPW_STATE_CLOSED,
	MLX5_MPW_STATE_OPENED,
	MLX5_MPW_STATE_OPENED_INL,
	MLX5_MPW_STATE_OPENING,
};
enum {
	MLX5_MAX_MPW_SGE = 5,
	MLX5_MAX_MPW_SIZE = 0x3FFF
};
struct mpw_data {
	uint8_t		state; /* use mpw_states */
	uint8_t		size;
	uint8_t		num_sge;
	uint32_t	len;
	uint32_t	total_len;
	uint32_t	flags;
	uint32_t	scur_post;
	union {
		struct mlx5_wqe_data_seg	*last_dseg;
		uint8_t				*inl_data;
	};
	uint32_t			*ctrl_update;
};
struct general_data_warm {
	uint32_t		 pattern;
	uint8_t			 qp_type;
};
struct odp_data {
	struct mlx5_pd		*pd;
};
struct data_seg_data {
	uint32_t	max_inline_data;
};
struct ctrl_seg_data {
	uint32_t	qp_num;
	uint8_t		fm_ce_se_tbl[8];
	uint8_t		fm_ce_se_acc[32];
	uint8_t		wq_sig;
};

enum mlx5_qp_flags {
	MLX5_QP_FLAGS_USE_UNDERLAY = 0x01,
};

struct mlx5_qp {
	struct mlx5_resource		rsc;
	struct verbs_qp			verbs_qp;
	struct mlx5_buf                 buf;
	int                             buf_size;
	/* For Raw Ethernet QP, use different Buffer for the SQ and RQ */
	struct mlx5_buf                 sq_buf;
	int				sq_buf_size;
	uint8_t	                        sq_signal_bits;
	int				umr_en;

	/* hot data used on data path */
	struct mlx5_wq			rq __MLX5_ALGN_D__;
	struct mlx5_wq			sq __MLX5_ALGN_D__;

	struct general_data_hot		gen_data;
	struct mpw_data			mpw;
	struct data_seg_data		data_seg;
	struct ctrl_seg_data		ctrl_seg;

	/* RAW_PACKET hot data */
	uint8_t				link_layer;

	/* used on data-path but not so hot */
	struct general_data_warm	gen_data_warm;
	/* atomic hot data */
	int				enable_atomics;
	/* odp hot data */
	struct odp_data			odp_data;
	/* ext atomic hot data */
	uint32_t			max_atomic_arg;
	/* umr hot data */
	uint32_t			max_inl_send_klms;
	/* recv-send enable hot data */
	struct mlx5_wq_recv_send_enable rq_enable;
	struct mlx5_wq_recv_send_enable sq_enable;
	int rx_qp;
	/* peer-direct data */
	int					peer_enabled;
	struct ibv_exp_peer_direct_attr	       *peer_ctx;
	void				       *peer_ctrl_seg;
	uint32_t				peer_scur_post;
	uint64_t				peer_va_ids[MLX5_QP_PEER_VA_ID_MAX];
	struct ibv_exp_peer_buf		       *peer_db_buf;
	uint32_t				max_tso_header;
	uint32_t                                flags; /* Use enum mlx5_qp_flags */
};

struct mlx5_dct {
	struct mlx5_resource		rsc;
	struct ibv_exp_dct		ibdct;
};

enum mlx5_wq_model_flags {
	/*
	 * When set the WQ API must be thread safe.
	 * When reset application is taking care
	 * to sync between WQ API calls.
	 */
	MLX5_WQ_MODEL_FLAG_THREAD_SAFE = 1 << 0,

	/*
	 * This flag is used to cache the IBV_EXP_DEVICE_RX_CSUM_IP_PKT
	 * device cap flag and it enables the related RX offloading support
	 */
	MLX5_WQ_MODEL_RX_CSUM_IP_OK_IP_NON_TCP_UDP = 1 << 1,
};

enum mlx5_mp_rq_sizes {
	/*
	 * Max log num of WQE strides supported by lib is 31 since related
	 * "num of strides" variables size (i.e. consumed_strides_counter[] and
	 * mp_rq_strides_in_wqe) is 32 bits
	 */
	MLX5_MP_RQ_MAX_LOG_NUM_STRIDES	= 31,
	/*
	 * Max log stride size supported by lib is 15 since related
	 * "stride size" variable size (i.e. mp_rq_stride_size) is 16 bits
	 */
	MLX5_MP_RQ_MAX_LOG_STRIDE_SIZE	= 15,
	MLX5_MP_RQ_SUPPORTED_QPT	= IBV_EXP_QPT_RAW_PACKET,
	MLX5_MP_RQ_SUPPORTED_SHIFTS	= IBV_EXP_MP_RQ_2BYTES_SHIFT
};

struct mlx5_rwq {
	struct mlx5_resource rsc;
	uint32_t pattern;
	struct ibv_exp_wq wq;
	struct mlx5_buf buf;
	int buf_size;
	/* hot data used on data path */
	struct mlx5_wq rq __MLX5_ALGN_D__;
	volatile uint32_t *db;
	/* Multi-Packet RQ hot data */
	/* Table to hold the consumed strides on each WQE */
	uint32_t *consumed_strides_counter;
	uint16_t mp_rq_stride_size;
	uint32_t mp_rq_strides_in_wqe;
	uint8_t mp_rq_packet_padding;
	/* recv-send enable hot data */
	struct mlx5_wq_recv_send_enable rq_enable;
	int wq_sig;
	uint8_t model_flags; /* use mlx5_wq_model_flags */
};

struct mlx5_ah {
	struct ibv_ah			ibv_ah;
	struct mlx5_wqe_av		av;
	int				kern_ah;
};

struct mlx5_verbs_srq {
	struct mlx5_srq msrq;
	struct verbs_srq vsrq;
};

struct mlx5_klm_buf {
	void					*buf;
	struct ibv_mr				*mr;
	struct ibv_exp_mkey_list_container	ibv_klm_list;
};

struct mlx5_send_db_data {
	struct mlx5_bf		bf;
	struct mlx5_wc_uar	*wc_uar;
	struct list_head	list;
};

/* Container for the dynamically allocated Write-Combining(WC) mapped UAR */
struct mlx5_wc_uar {
	/* Each UAR contains MLX5_NUM_NON_FP_BFREGS_PER_UAR BF regs */
	struct mlx5_send_db_data	*send_db_data;
	/* The index used to mmap this UAR */
	int				uar_idx;
	/* The virtual address of the WC mmaped UAR */
	void				*uar;
	struct list_head		list;
};

struct mlx5_res_domain {
	struct ibv_exp_res_domain		 ibv_res_domain;
	struct ibv_exp_res_domain_init_attr	 attr;
	struct mlx5_send_db_data		*send_db;
};

static inline int mlx5_ilog2(int n)
{
	int t;

	if (n <= 0)
		return -1;

	t = 0;
	while ((1 << t) < n)
		++t;

	return t;
}

extern int mlx5_stall_num_loop;
extern int mlx5_stall_cq_poll_min;
extern int mlx5_stall_cq_poll_max;
extern int mlx5_stall_cq_inc_step;
extern int mlx5_stall_cq_dec_step;
extern int mlx5_single_threaded;
extern int mlx5_use_mutex;

#ifdef MLX5_DEBUG
void dump_wqe(FILE *fp, int idx, int size_16, struct mlx5_qp *qp);
#endif

static inline void *aligned_calloc(size_t size)
{
	void *p;

	if (posix_memalign(&p, MLX5_D_F_ALGN_SIZE, size))
		return NULL;

	memset(p, 0, size);

	return p;
}

static inline unsigned DIV_ROUND_UP(unsigned n, unsigned d)
{
	return (n + d - 1u) / d;
}

static inline unsigned long align(unsigned long val, unsigned long algn)
{
	return (val + algn - 1) & ~(algn - 1);
}

static inline void *align_ptr(void *p, unsigned long algn)
{
	return (void *)align((unsigned long)p, algn);
}

#define to_mxxx(xxx, type)						\
	((struct mlx5_##type *)					\
	 ((void *) ib##xxx - offsetof(struct mlx5_##type, ibv_##xxx)))

static inline struct mlx5_device *to_mdev(struct ibv_device *ibdev)
{
	struct mlx5_device *ret;

	ret = (void *)ibdev - offsetof(struct mlx5_device, verbs_dev);

	return ret;
}

static inline struct mlx5_context *to_mctx(struct ibv_context *ibctx)
{
	return to_mxxx(ctx, context);
}

static inline struct mlx5_pd *to_mpd(struct ibv_pd *ibpd)
{
	return to_mxxx(pd, pd);
}

static inline struct mlx5_cq *to_mcq(struct ibv_cq *ibcq)
{
	return to_mxxx(cq, cq);
}

static inline struct mlx5_srq *to_msrq(struct ibv_srq *ibsrq)
{
	struct verbs_srq *vsrq = (struct verbs_srq *)ibsrq;

	return container_of(vsrq, struct mlx5_srq, vsrq);
}

static inline struct mlx5_qp *to_mqp(struct ibv_qp *ibqp)
{
	struct verbs_qp *vqp = (struct verbs_qp *)ibqp;

	return container_of(vqp, struct mlx5_qp, verbs_qp);
}

static inline struct mlx5_dct *to_mdct(struct ibv_exp_dct *ibdct)
{
	return container_of(ibdct, struct mlx5_dct, ibdct);
}

static inline struct mlx5_rwq *to_mrwq(struct ibv_exp_wq *ibwq)
{
	return container_of(ibwq, struct mlx5_rwq, wq);
}

static inline struct mlx5_dm *to_mdm(struct ibv_exp_dm *ibdm)
{
	return container_of(ibdm, struct mlx5_dm, ibdm);
}

static inline struct mlx5_mr *to_mmr(struct ibv_mr *ibmr)
{
	return to_mxxx(mr, mr);
}

static inline struct mlx5_ah *to_mah(struct ibv_ah *ibah)
{
	return to_mxxx(ah, ah);
}

static inline struct mlx5_res_domain *to_mres_domain(struct ibv_exp_res_domain *ibres_domain)
{
	return to_mxxx(res_domain, res_domain);
}

static inline struct mlx5_klm_buf *to_klm(struct ibv_exp_mkey_list_container  *ibklm)
{
	size_t off = offsetof(struct mlx5_klm_buf, ibv_klm_list);

	return (struct mlx5_klm_buf *)((void *)ibklm - off);
}

static inline int max_int(int a, int b)
{
	return a > b ? a : b;
}

static inline enum mlx5_lock_type mlx5_get_locktype(void)
{
	if (!mlx5_use_mutex)
		return MLX5_SPIN_LOCK;
	return MLX5_MUTEX;
}

void *mlx5_uar_mmap(int idx, int cmd, int page_size, int cmd_fd);
int mlx5_cpu_local_numa(void);
void mlx5_build_ctrl_seg_data(struct mlx5_qp *qp, uint32_t qp_num);
int mlx5_alloc_buf(struct mlx5_buf *buf, size_t size, int page_size);
void mlx5_free_buf(struct mlx5_buf *buf);
int mlx5_alloc_buf_contig(struct mlx5_context *mctx, struct mlx5_buf *buf,
			  size_t size, int page_size, const char *component, void *req_addr);
void mlx5_free_buf_contig(struct mlx5_context *mctx, struct mlx5_buf *buf);
int mlx5_alloc_preferred_buf(struct mlx5_context *mctx,
			     struct mlx5_buf *buf,
			     size_t size, int page_size,
			     enum mlx5_alloc_type alloc_type,
			     const char *component);
int mlx5_free_actual_buf(struct mlx5_context *ctx, struct mlx5_buf *buf);
void mlx5_get_alloc_type(struct ibv_context *context,
			 const char *component,
			 enum mlx5_alloc_type *alloc_type,
			 enum mlx5_alloc_type default_alloc_type);
int mlx5_use_huge(struct ibv_context *context, const char *key);

uint32_t *mlx5_alloc_dbrec(struct mlx5_context *context);
void mlx5_free_db(struct mlx5_context *context, volatile uint32_t *db);

int mlx5_prefetch_mr(struct ibv_mr *mr, struct ibv_exp_prefetch_attr *attr);

int mlx5_query_device(struct ibv_context *context,
		       struct ibv_device_attr *attr);
int mlx5_query_device_ex(struct ibv_context *context,
			 const struct ibv_query_device_ex_input *input,
			 struct ibv_device_attr_ex *attr,
			 size_t attr_size);
int mlx5_query_port(struct ibv_context *context, uint8_t port,
		     struct ibv_port_attr *attr);
int mlx5_exp_query_port(struct ibv_context *context, uint8_t port_num,
			struct ibv_exp_port_attr *port_attr);

struct ibv_pd *mlx5_alloc_pd(struct ibv_context *context);
int mlx5_free_pd(struct ibv_pd *pd);
void read_init_vars(struct mlx5_context *ctx);

struct ibv_mr *mlx5_reg_mr(struct ibv_pd *pd, void *addr,
			   size_t length, int access);
int mlx5_rereg_mr(struct ibv_mr *mr, int flags, struct ibv_pd *pd, void *addr,
		  size_t length, int access);
struct ibv_mr *mlx5_exp_reg_mr(struct ibv_exp_reg_mr_in *in);
int mlx5_dereg_mr(struct ibv_mr *mr);
struct ibv_mw *mlx5_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type);
int mlx5_dealloc_mw(struct ibv_mw *mw);
int mlx5_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
		 struct ibv_mw_bind *mw_bind);
struct ibv_exp_dm *mlx5_exp_alloc_dm(struct ibv_context *context,
				     struct ibv_exp_alloc_dm_attr *dm_attr);
int mlx5_exp_free_dm(struct ibv_exp_dm *ibdm);
int mlx5_exp_memcpy_dm(struct ibv_exp_dm *ibdm,
		       struct ibv_exp_memcpy_dm_attr *memcpy_dm_attr);
struct ibv_cq *mlx5_create_cq(struct ibv_context *context, int cqe,
			       struct ibv_comp_channel *channel,
			       int comp_vector);
struct ibv_cq *mlx5_create_cq_ex(struct ibv_context *context,
				 int cqe,
				 struct ibv_comp_channel *channel,
				 int comp_vector,
				 struct ibv_exp_cq_init_attr *attr);
int mlx5_alloc_cq_buf(struct mlx5_context *mctx, struct mlx5_cq *cq,
		      struct mlx5_buf *buf, int nent, int cqe_sz);
int mlx5_alloc_cq_peer_buf(struct mlx5_context *ctx, struct mlx5_cq *cq, int n);
int mlx5_resize_cq(struct ibv_cq *cq, int cqe);
int mlx5_destroy_cq(struct ibv_cq *cq);
int mlx5_poll_cq(struct ibv_cq *cq, int ne, struct ibv_wc *wc) __MLX5_ALGN_F__;
int mlx5_poll_cq_1(struct ibv_cq *cq, int ne, struct ibv_wc *wc) __MLX5_ALGN_F__;
int mlx5_arm_cq(struct ibv_cq *cq, int solicited);
void mlx5_cq_event(struct ibv_cq *cq);
void __mlx5_cq_clean(struct mlx5_cq *cq, uint32_t qpn, struct mlx5_srq *srq);
void mlx5_cq_clean(struct mlx5_cq *cq, uint32_t qpn, struct mlx5_srq *srq);
void mlx5_cq_resize_copy_cqes(struct mlx5_cq *cq);

struct ibv_srq *mlx5_create_srq(struct ibv_pd *pd,
				 struct ibv_srq_init_attr *attr);
int mlx5_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *attr,
		    int mask);
int mlx5_query_srq(struct ibv_srq *srq,
			   struct ibv_srq_attr *attr);
int mlx5_destroy_srq(struct ibv_srq *srq);
int mlx5_alloc_srq_buf(struct ibv_context *context, struct mlx5_srq *srq);
void mlx5_free_srq_wqe(struct mlx5_srq *srq, int ind);
struct mlx5_srq *mlx5_alloc_srq(struct ibv_context *context,
				struct ibv_srq_attr *attr);
void mlx5_free_srq(struct ibv_context *context, struct mlx5_srq *srq);
int mlx5_post_srq_recv(struct ibv_srq *ibsrq,
		       struct ibv_recv_wr *wr,
		       struct ibv_recv_wr **bad_wr) __MLX5_ALGN_F__;

struct ibv_qp *mlx5_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr);
int mlx5_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		  int attr_mask,
		  struct ibv_qp_init_attr *init_attr);
int mlx5_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		   int attr_mask);
int mlx5_destroy_qp(struct ibv_qp *qp);
void mlx5_init_qp_indices(struct mlx5_qp *qp);
void mlx5_init_rwq_indices(struct mlx5_rwq *rwq);
void mlx5_update_post_send_one(struct mlx5_qp *qp, enum ibv_qp_state qp_state, enum ibv_qp_type	qp_type);
int mlx5_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr) __MLX5_ALGN_F__;
int mlx5_exp_post_send(struct ibv_qp *ibqp, struct ibv_exp_send_wr *wr,
		       struct ibv_exp_send_wr **bad_wr) __MLX5_ALGN_F__;
struct ibv_exp_mkey_list_container *mlx5_alloc_mkey_mem(struct ibv_exp_mkey_list_container_attr *attr);
int mlx5_free_mkey_mem(struct ibv_exp_mkey_list_container *mem);
int mlx5_query_mkey(struct ibv_mr *mr, struct ibv_exp_mkey_attr *mkey_attr);
struct ibv_mr *mlx5_create_mr(struct ibv_exp_create_mr_in *in);
int mlx5_exp_dereg_mr(struct ibv_mr *mr, struct ibv_exp_dereg_out *out);
struct ibv_exp_wq *mlx5_exp_create_wq(struct ibv_context *context,
				      struct ibv_exp_wq_init_attr *attr);
int mlx5_exp_modify_wq(struct ibv_exp_wq *wq, struct ibv_exp_wq_attr *attr);
int mlx5_exp_destroy_wq(struct ibv_exp_wq *wq);
struct ibv_exp_rwq_ind_table *mlx5_exp_create_rwq_ind_table(struct ibv_context *context,
							    struct ibv_exp_rwq_ind_table_init_attr *init_attr);
int mlx5_exp_destroy_rwq_ind_table(struct ibv_exp_rwq_ind_table *rwq_ind_table);
int mlx5_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
			  struct ibv_recv_wr **bad_wr) __MLX5_ALGN_F__;
void mlx5_calc_sq_wqe_size(struct ibv_qp_cap *cap, enum ibv_qp_type type,
			   struct mlx5_qp *qp);
void mlx5_set_sq_sizes(struct mlx5_qp *qp, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type);
int mlx5_store_rsc(struct mlx5_context *ctx, uint32_t rsn, void *rsc);
void *mlx5_find_rsc(struct mlx5_context *ctx, uint32_t rsn);
void mlx5_clear_rsc(struct mlx5_context *ctx, uint32_t rsn);
uint32_t mlx5_store_uidx(struct mlx5_context *ctx, void *rsc);
void mlx5_clear_uidx(struct mlx5_context *ctx, uint32_t uidx);
struct mlx5_srq *mlx5_find_srq(struct mlx5_context *ctx, uint32_t srqn);
int mlx5_store_srq(struct mlx5_context *ctx, uint32_t srqn,
		   struct mlx5_srq *srq);
void mlx5_clear_srq(struct mlx5_context *ctx, uint32_t srqn);
struct ibv_ah *mlx5_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
int mlx5_destroy_ah(struct ibv_ah *ah);
int mlx5_alloc_av(struct mlx5_pd *pd, struct ibv_ah_attr *attr,
		   struct mlx5_ah *ah);
void mlx5_free_av(struct mlx5_ah *ah);
int mlx5_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid);
int mlx5_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid);
int mlx5_round_up_power_of_two(long long sz);
void *mlx5_get_atomic_laddr(struct mlx5_qp *qp, uint16_t idx, int *byte_count);
int mlx5_copy_to_recv_wqe(struct mlx5_qp *qp, int idx, void *buf, int size);
int mlx5_copy_to_send_wqe(struct mlx5_qp *qp, int idx, void *buf, int size);
int mlx5_poll_dc_info(struct ibv_context *context,
		      struct ibv_exp_dc_info_ent *ents,
		      int nent, int port);
int mlx5_copy_to_recv_srq(struct mlx5_srq *srq, int idx, void *buf, int size);
struct ibv_qp *mlx5_drv_create_qp(struct ibv_context *context,
				  struct ibv_qp_init_attr_ex *attrx);
struct ibv_qp *mlx5_exp_create_qp(struct ibv_context *context,
				  struct ibv_exp_qp_init_attr *attrx);
struct ibv_ah *mlx5_exp_create_ah(struct ibv_pd *pd,
				  struct ibv_exp_ah_attr *attr_ex);
struct ibv_ah *mlx5_exp_create_kah(struct ibv_pd *pd,
				   struct ibv_exp_ah_attr *attr_ex);
struct ibv_xrcd	*mlx5_open_xrcd(struct ibv_context *context,
				struct ibv_xrcd_init_attr *xrcd_init_attr);
struct ibv_srq *mlx5_create_srq_ex(struct ibv_context *context,
				   struct ibv_srq_init_attr_ex *attr_ex);
struct ibv_srq *mlx5_exp_create_srq(struct ibv_context *context,
				    struct ibv_exp_create_srq_attr *attr);
int mlx5_get_srq_num(struct ibv_srq *srq, uint32_t *srq_num);
int mlx5_exp_post_srq_ops(struct ibv_srq *srq,
			  struct ibv_exp_ops_wr *wr,
			  struct ibv_exp_ops_wr **bad_wr);
struct ibv_qp *mlx5_open_qp(struct ibv_context *context,
			    struct ibv_qp_open_attr *attr);
int mlx5_close_xrcd(struct ibv_xrcd *ib_xrcd);
int mlx5_modify_qp_ex(struct ibv_qp *qp, struct ibv_exp_qp_attr *attr,
		      uint64_t attr_mask);
void *mlx5_get_legacy_xrc(struct ibv_srq *srq);
void mlx5_set_legacy_xrc(struct ibv_srq *srq, void *legacy_xrc_srq);
int mlx5_exp_query_device(struct ibv_context *context,
			 struct ibv_exp_device_attr *attr);
int mlx5_exp_query_values(struct ibv_context *context, int q_values,
			  struct ibv_exp_values *values);
int mlx5_modify_cq(struct ibv_cq *cq, struct ibv_exp_cq_attr *attr, int attr_mask);
struct ibv_exp_dct *mlx5_create_dct(struct ibv_context *context,
				    struct ibv_exp_dct_init_attr *attr);
int mlx5_destroy_dct(struct ibv_exp_dct *dct);
int mlx5_poll_cq_ex(struct ibv_cq *ibcq, int num_entries,
		    struct ibv_exp_wc *wc, uint32_t wc_size) __MLX5_ALGN_F__;
int mlx5_poll_cq_ex_1(struct ibv_cq *ibcq, int num_entries,
		      struct ibv_exp_wc *wc, uint32_t wc_size) __MLX5_ALGN_F__;
int mlx5_query_dct(struct ibv_exp_dct *dct, struct ibv_exp_dct_attr *attr);
int mlx5_arm_dct(struct ibv_exp_dct *dct, struct ibv_exp_arm_attr *attr);
int mlx5_post_task(struct ibv_context *context,
			struct ibv_exp_task *task_list,
			struct ibv_exp_task **bad_task);
struct ibv_exp_res_domain *mlx5_exp_create_res_domain(struct ibv_context *context,
						      struct ibv_exp_res_domain_init_attr *attr);
int mlx5_exp_destroy_res_domain(struct ibv_context *context,
				struct ibv_exp_res_domain *res_dom,
				struct ibv_exp_destroy_res_domain_attr *attr);
void *mlx5_exp_query_intf(struct ibv_context *context, struct ibv_exp_query_intf_params *params,
			  enum ibv_exp_query_intf_status *status);
int mlx5_exp_release_intf(struct ibv_context *context, void *intf,
			  struct ibv_exp_release_intf_params *params);
struct ibv_exp_qp_burst_family_v1 *mlx5_get_qp_burst_family(struct mlx5_qp *qp,
							 struct ibv_exp_query_intf_params *params,
							 enum ibv_exp_query_intf_status *status);
struct ibv_exp_wq_family *mlx5_get_wq_family(struct mlx5_rwq *rwq,
					     struct ibv_exp_query_intf_params *params,
					     enum ibv_exp_query_intf_status *status);
struct ibv_exp_cq_family_v1 *mlx5_get_poll_cq_family(struct mlx5_cq *cq,
						     struct ibv_exp_query_intf_params *params,
						     enum ibv_exp_query_intf_status *status);
int mlx5_exp_peer_commit_qp(struct ibv_qp *qp,
			    struct ibv_exp_peer_commit *peer);
int mlx5_exp_rollback_send(struct ibv_qp *ibqp,
			   struct ibv_exp_rollback_ctx *rollback);
int mlx5_exp_peer_peek_cq(struct ibv_cq *cq,
			  struct ibv_exp_peer_peek *peek_ctx);
int mlx5_exp_peer_abort_peek_cq(struct ibv_cq *ibcq,
				struct ibv_exp_peer_abort_peek *ack_ctx);
int mlx5_exp_set_context_attr(struct ibv_context *context,
			      struct ibv_exp_open_device_attr *attr);

static inline void *mlx5_find_uidx(struct mlx5_context *ctx, uint32_t uidx)
{
	int tind = uidx >> MLX5_QP_TABLE_SHIFT;

	if (likely(ctx->uidx_table[tind].refcnt))
		return ctx->uidx_table[tind].table[uidx & MLX5_QP_TABLE_MASK];

	return NULL;
}

static inline int mlx5_spin_lock(struct mlx5_spinlock *lock)
{
	if (lock->state == MLX5_USE_LOCK)
		return pthread_spin_lock(&lock->lock);

	if (unlikely(lock->state == MLX5_LOCKED)) {
		fprintf(stderr, "*** ERROR: multithreading violation ***\n"
			"You are running a multithreaded application but\n"
			"you set MLX5_SINGLE_THREADED=1 or created a\n"
			"resource domain thread-model which is not safe.\n"
			"Please fix it.\n");
		abort();
	} else {
		lock->state = MLX5_LOCKED;
		wmb();
	}

	return 0;
}

static inline int mlx5_spin_unlock(struct mlx5_spinlock *lock)
{
	if (lock->state == MLX5_USE_LOCK)
		return pthread_spin_unlock(&lock->lock);

	lock->state = MLX5_UNLOCKED;

	return 0;
}

static inline int mlx5_spinlock_init(struct mlx5_spinlock *lock, int use_spinlock)
{
	if (use_spinlock) {
		lock->state = MLX5_USE_LOCK;
		return pthread_spin_init(&lock->lock, PTHREAD_PROCESS_PRIVATE);
	}
	lock->state = MLX5_UNLOCKED;

	return 0;
}

static inline int mlx5_spinlock_destroy(struct mlx5_spinlock *lock)
{
	if (lock->state == MLX5_USE_LOCK)
		return pthread_spin_destroy(&lock->lock);

	return 0;
}

static inline int mlx5_lock(struct mlx5_lock *lock)
{
	if (lock->state == MLX5_USE_LOCK) {
		if (lock->type == MLX5_SPIN_LOCK)
			return pthread_spin_lock(&lock->slock);

		return pthread_mutex_lock(&lock->mutex);
	}

	if (unlikely(lock->state == MLX5_LOCKED)) {
		fprintf(stderr, "*** ERROR: multithreading violation ***\n"
			"You are running a multithreaded application but\n"
			"you set MLX5_SINGLE_THREADED=1 or created a\n"
			"resource domain thread-model which is not safe.\n"
			"Please fix it.\n");
		abort();
	} else {
		lock->state = MLX5_LOCKED;
		/* Make new lock state visible to other threads */
		wmb();
	}

	return 0;
}

static inline int mlx5_unlock(struct mlx5_lock *lock)
{
	if (lock->state == MLX5_USE_LOCK) {
		if (lock->type == MLX5_SPIN_LOCK)
			return pthread_spin_unlock(&lock->slock);

		return pthread_mutex_unlock(&lock->mutex);
	}

	lock->state = MLX5_UNLOCKED;

	return 0;
}

static inline int mlx5_lock_init(struct mlx5_lock *lock,
				 int use_lock,
				 enum mlx5_lock_type lock_type)
{
	if (use_lock) {
		lock->type = lock_type;
		lock->state = MLX5_USE_LOCK;
		if (lock->type == MLX5_SPIN_LOCK)
			return pthread_spin_init(&lock->slock,
						 PTHREAD_PROCESS_PRIVATE);
		return pthread_mutex_init(&lock->mutex,
					  PTHREAD_PROCESS_PRIVATE);
	}

	lock->state = MLX5_UNLOCKED;

	return 0;
}

static inline int mlx5_lock_destroy(struct mlx5_lock *lock)
{
	if (lock->state == MLX5_USE_LOCK) {
		if (lock->type == MLX5_SPIN_LOCK)
			return pthread_spin_destroy(&lock->slock);

		return pthread_mutex_destroy(&lock->mutex);
	}
	return 0;
}

static inline void set_command(int command, off_t *offset)
{
	*offset |= (command << MLX5_IB_MMAP_CMD_SHIFT);
}

static inline int get_command(off_t *offset)
{
	return ((*offset >> MLX5_IB_MMAP_CMD_SHIFT) & MLX5_IB_MMAP_CMD_MASK);
}

static inline void reset_command(off_t *offset)
{
	*offset &= ~(MLX5_IB_MMAP_CMD_MASK << MLX5_IB_MMAP_CMD_SHIFT);
}

static inline void set_arg(int arg, off_t *offset)
{
	*offset |= arg;
}

static inline void set_order(int order, off_t *offset)
{
	set_arg(order, offset);
}

static inline void set_index(int index, off_t *offset)
{
	set_arg(index, offset);
}

static inline uint8_t calc_xor(void *wqe, int size)
{
	int i;
	uint8_t *p = wqe;
	uint8_t res = 0;

	for (i = 0; i < size; ++i)
		res ^= p[i];

	return res;
}

static inline void mlx5_update_cons_index(struct mlx5_cq *cq)
{
	cq->dbrec[MLX5_CQ_SET_CI] = htonl(cq->cons_index & 0xffffff);
}

static inline void set_ctrl_seg(uint32_t *start, struct ctrl_seg_data *ctrl_seg,
				uint8_t opcode, uint16_t idx, uint8_t opmod,
				uint8_t size, uint8_t fm_ce_se, uint32_t imm_invk_umrk)
{
	*start++ = htonl(opmod << 24 | idx << 8 | opcode);
	*start++ = htonl(ctrl_seg->qp_num << 8 | (size & 0x3F));
	*start++ = htonl(fm_ce_se);
	*start = imm_invk_umrk;
}

static inline void *mlx5_get_send_wqe(struct mlx5_qp *qp, int n)
{
	return qp->gen_data.sqstart + (n << MLX5_SEND_WQE_SHIFT);
}

#endif /* MLX5_H */
