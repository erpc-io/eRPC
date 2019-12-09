/**
 * @file mlx5_defs.h
 * @brief Mellanox headers from Mellanox OFED 4.2
 */

#pragma once

#ifndef ERPC_DPDK

#include <infiniband/verbs.h>
#include <inttypes.h>
#include <linux/types.h>
#include <pthread.h>
#include <stdint.h>

namespace erpc {

enum {
  MLX5_IPOIB_INLINE_MIN_HEADER_SIZE = 4,
  MLX5_IPOIB_INLINE_MAX_HEADER_SIZE = 18,
  MLX5_ETH_INLINE_HEADER_SIZE = 18,
  MLX5_ETH_VLAN_INLINE_HEADER_SIZE = 18,
  MLX5_ETH_L2_MIN_HEADER_SIZE = 14,
};

// From Mellanox OFED 4.2 for Ubuntu 17.04
enum mlx5_alloc_type {
  MLX5_ALLOC_TYPE_ANON,
  MLX5_ALLOC_TYPE_HUGE,
  MLX5_ALLOC_TYPE_CONTIG,
  MLX5_ALLOC_TYPE_PEER_DIRECT,
  MLX5_ALLOC_TYPE_PREFER_HUGE,
  MLX5_ALLOC_TYPE_PREFER_CONTIG,
  MLX5_ALLOC_TYPE_ALL
};

enum mlx5_lock_type {
  MLX5_SPIN_LOCK = 0,
  MLX5_MUTEX = 1,
};

enum mlx5_lock_state { MLX5_USE_LOCK, MLX5_LOCKED, MLX5_UNLOCKED };

struct mlx5_lock {
  pthread_mutex_t mutex;
  pthread_spinlock_t slock;
  enum mlx5_lock_state state;
  enum mlx5_lock_type type;
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
  void *buf;
  size_t length;
  int base;
  struct mlx5_hugetlb_mem *hmem;
  struct mlx5_peer_direct_mem peer;
  enum mlx5_alloc_type type;
  struct mlx5_numa_req numa_req;
  int numa_alloc;
};

struct mlx5_mini_cqe8 {
  union {
    uint32_t rx_hash_result;
    uint32_t checksum;
    struct {
      uint16_t wqe_counter;
      uint8_t s_wqe_opcode;
      uint8_t reserved;
    } s_wqe_info;
  };
  uint32_t byte_cnt;
};

enum { MLX5_MINI_ARR_SIZE = 8 };

struct mlx5_tm_cqe {
  uint32_t success;
  uint32_t hw_phase_cnt;
  uint8_t rsvd0[10];
};

struct mlx5_cqe64 {
  uint8_t rsvd0[2];
  /*
   * wqe_id is valid only for
   * Striding RQ (Multi-Packet RQ).
   * It provides the WQE index inside the RQ.
   */
  uint16_t wqe_id;
  uint8_t rsvd4[8];
  uint32_t rx_hash_res;
  uint8_t rx_hash_type;
  uint8_t ml_path;
  uint8_t rsvd20[2];
  uint16_t checksum;
  uint16_t slid;
  uint32_t flags_rqpn;
  uint8_t hds_ip_ext;
  uint8_t l4_hdr_type_etc;
  __be16 vlan_info;
  uint32_t srqn_uidx;
  uint32_t imm_inval_pkey;
  uint8_t app;
  uint8_t app_op;
  uint16_t app_info;
  uint32_t byte_cnt;
  __be64 timestamp;
  union {
    uint32_t sop_drop_qpn;
    struct {
      uint8_t sop;
      uint8_t qpn[3];
    } sop_qpn;
  };
  /*
   * In Striding RQ (Multi-Packet RQ) wqe_counter provides
   * the WQE stride index (to calc pointer to start of the message)
   */
  uint16_t wqe_counter;
  uint8_t signature;
  uint8_t op_own;
};

struct mlx5_cq {
  struct ibv_cq ibv_cq;
  uint32_t creation_flags;
  uint32_t pattern;
  struct mlx5_buf buf_a;
  struct mlx5_buf buf_b;
  struct mlx5_buf *active_buf;
  struct mlx5_buf *resize_buf;
  int resize_cqes;
  int active_cqes;
  struct mlx5_lock lock;
  uint32_t cqn;
  uint32_t cons_index;
  uint32_t wait_index;
  uint32_t wait_count;
  volatile uint32_t *dbrec;
  int arm_sn;
  int cqe_sz;
  int resize_cqe_sz;
  int stall_next_poll;
  int stall_enable;
  uint64_t stall_last_count;
  int stall_adaptive_enable;
  int stall_cycles;
  uint8_t model_flags; /* use mlx5_cq_model_flags */
  uint16_t cqe_comp_max_num;
  uint8_t cq_log_size;
  /* Compressed CQE data */
  struct mlx5_cqe64 next_decomp_cqe64;
  struct mlx5_resource *compressed_rsc;
  uint16_t compressed_left;
  uint16_t compressed_wqe_cnt;
  uint8_t compressed_req;
  uint8_t compressed_mp_rq;
  uint8_t mini_arr_idx;
  struct mlx5_mini_cqe8 mini_array[MLX5_MINI_ARR_SIZE];
  /* peer-direct data */
  int peer_enabled;
  struct ibv_exp_peer_direct_attr *peer_ctx;
  struct mlx5_buf peer_buf;
  struct mlx5_peek_entry **peer_peek_table;
  struct mlx5_peek_entry *peer_peek_free;
};

}  // namespace erpc

#endif
