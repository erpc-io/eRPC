/**
 * @file mlx5_defs.h
 * @brief Mellanox headers from Mellanox OFED 4.2
 */

#ifndef ERPC_MLX5_DEFS_H
#define ERPC_MLX5_DEFS_H

#include <infiniband/verbs_exp.h>
#include <inttypes.h>
#include <stdint.h>

namespace erpc {

struct mlx5_cqe64 {
  uint8_t rsvd0[2];
  /*
   * wqe_id is valid only for Striding RQ (Multi-Packet RQ).
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
  uint16_t vlan_info;  // Originally __be16, but where's the header?
  uint32_t srqn_uidx;
  uint32_t imm_inval_pkey;
  uint8_t rsvd40[4];
  uint32_t byte_cnt;
  uint64_t timestamp;  // Originally __be64, but where's the header?
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
static_assert(sizeof(mlx5_cqe64) == 64, "");

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

enum mlx5_alloc_type {
  MLX5_ALLOC_TYPE_ANON,
  MLX5_ALLOC_TYPE_HUGE,
  MLX5_ALLOC_TYPE_CONTIG,
  MLX5_ALLOC_TYPE_PREFER_HUGE,
  MLX5_ALLOC_TYPE_PREFER_CONTIG,
  MLX5_ALLOC_TYPE_ALL
};

struct mlx5_numa_req {
  int valid;
  int numa_id;
};

struct mlx5_buf {
  void* buf;
  size_t length;
  int base;
  struct mlx5_hugetlb_mem* hmem;
  enum mlx5_alloc_type type;
  struct mlx5_numa_req numa_req;
  int numa_alloc;
};

struct mlx5_cq {
  struct ibv_cq ibv_cq;
  uint32_t creation_flags;
  uint32_t pattern;
  struct mlx5_buf buf_a;
  struct mlx5_buf buf_b;
  struct mlx5_buf* active_buf;
  struct mlx5_buf* resize_buf;
  int resize_cqes;
  int active_cqes;
  struct mlx5_lock lock;
  uint32_t cqn;
  uint32_t cons_index;
  uint32_t wait_index;
  uint32_t wait_count;
  volatile uint32_t* dbrec;
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
};

}  // End erpc

#endif  // ERPC_MLX5_DEFS_H
