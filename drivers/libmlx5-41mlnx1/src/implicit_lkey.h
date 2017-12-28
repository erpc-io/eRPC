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

#ifndef IMPLICIT_LKEY_H
#define IMPLICIT_LKEY_H

#include <stdint.h>


#define ODP_GLOBAL_R_LKEY 0x00000101
#define ODP_GLOBAL_W_LKEY 0x00000102
#define MLX5_WHOLE_ADDR_SPACE (~((size_t)0))

struct mlx5_pd;
struct ibv_exp_reg_mr_in;

struct mlx5_pair_mrs {
	struct ibv_mr *mrs[2];
};

struct mlx5_implicit_lkey {
	struct mlx5_pair_mrs **table;
	uint64_t exp_access;
	pthread_mutex_t lock;
};

int mlx5_init_implicit_lkey(struct mlx5_implicit_lkey *ilkey,
			    uint64_t access_flags);

void mlx5_destroy_implicit_lkey(struct mlx5_implicit_lkey *ilkey);
struct mlx5_implicit_lkey *mlx5_get_implicit_lkey(struct mlx5_pd *pd, uint64_t exp_access);

struct ibv_mr *mlx5_alloc_whole_addr_mr(const struct ibv_exp_reg_mr_in *attr);

void mlx5_dealloc_whole_addr_mr(struct ibv_mr *);

int mlx5_get_real_lkey_from_implicit_lkey(struct mlx5_pd *pd,
					  struct mlx5_implicit_lkey *ilkey,
					  uint64_t addr, size_t len,
					  uint32_t *lkey);
int mlx5_get_real_mr_from_implicit_lkey(struct mlx5_pd *pd,
					struct mlx5_implicit_lkey *ilkey,
					uint64_t addr, uint64_t len,
					struct ibv_mr **mr);

int mlx5_prefetch_implicit_lkey(struct mlx5_pd *pd,
				struct mlx5_implicit_lkey *ilkey,
				uint64_t addr, size_t len, int flags);

#endif /* IMPLICIT_LKEY_H */
