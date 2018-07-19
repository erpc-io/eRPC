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

#ifndef WQE_H
#define WQE_H

enum {
	MLX5_FENCE_MODE_NONE			= 0 << 5,
	MLX5_FENCE_MODE_INITIATOR_SMALL		= 1 << 5,
	MLX5_FENCE_MODE_STRONG_ORDERING		= 3 << 5,
	MLX5_FENCE_MODE_SMALL_AND_FENCE		= 4 << 5,
};

struct mlx5_eqe_comp {
	uint32_t	reserved[6];
	uint32_t	cqn;
};

struct mlx5_eqe_qp_srq {
	uint32_t	reserved[6];
	uint32_t	qp_srq_n;
};

enum {
	MLX5_IPOIB_INLINE_MIN_HEADER_SIZE	= 4,
	MLX5_IPOIB_INLINE_MAX_HEADER_SIZE	= 18,
	MLX5_ETH_INLINE_HEADER_SIZE		= 18,
	MLX5_ETH_VLAN_INLINE_HEADER_SIZE	= 18,
	MLX5_ETH_L2_MIN_HEADER_SIZE		= 14,
};

struct mlx5_wqe_eth_pad {
	uint8_t rsvd0[16];
};

struct mlx5_wqe_xrc_seg {
	uint32_t	xrc_srqn;
	uint8_t		rsvd[12];
};

struct mlx5_wqe_masked_atomic_seg {
	uint64_t	swap_add;
	uint64_t	compare;
	uint64_t	swap_add_mask;
	uint64_t	compare_mask;
};

enum {
	MLX5_WQE_UMR_CTRL_FLAG_INLINE =			1 << 7,
	MLX5_WQE_UMR_CTRL_FLAG_CHECK_FREE =		1 << 5,
	MLX5_WQE_UMR_CTRL_FLAG_TRNSLATION_OFFSET =	1 << 4,
	MLX5_WQE_UMR_CTRL_FLAG_CHECK_QPN =		1 << 3,
};

enum {
	MLX5_WQE_UMR_CTRL_MKEY_MASK_LEN			= 1 << 0,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_START_ADDR		= 1 << 6,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_MKEY		= 1 << 13,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_QPN			= 1 << 14,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_LOCAL_WRITE	= 1 << 18,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_READ	= 1 << 19,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_WRITE	= 1 << 20,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_ATOMIC	= 1 << 21,
	MLX5_WQE_UMR_CTRL_MKEY_MASK_FREE		= 1 << 29,
};

struct mlx5_wqe_umr_ctrl_seg {
	uint8_t		flags;
	uint8_t		rsvd0[3];
	uint16_t	klm_octowords;
	uint16_t	translation_offset;
	uint64_t	mkey_mask;
	uint8_t		rsvd1[32];
};

struct mlx5_wqe_umr_klm_seg {
	/* up to 2GB */
	uint32_t	byte_count;
	uint32_t	mkey;
	uint64_t	address;
};

union mlx5_wqe_umr_inline_seg {
	struct mlx5_wqe_umr_klm_seg	klm;
};

enum {
	MLX5_WQE_MKEY_CONTEXT_FREE = 1 << 6
};

enum {
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_ATOMIC = 1 << 6,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_WRITE = 1 << 5,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_READ = 1 << 4,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_LOCAL_WRITE = 1 << 3,
	MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_LOCAL_READ = 1 << 2
};

struct mlx5_wqe_mkey_context_seg {
	uint8_t		free;
	uint8_t		reserved1;
	uint8_t		access_flags;
	uint8_t		sf;
	uint32_t	qpn_mkey;
	uint32_t	reserved2;
	uint32_t	flags_pd;
	uint64_t	start_addr;
	uint64_t	len;
	uint32_t	bsf_octword_size;
	uint32_t	reserved3[4];
	uint32_t	translations_octword_size;
	uint8_t		reserved4[3];
	uint8_t		log_page_size;
	uint32_t	reserved;
	union mlx5_wqe_umr_inline_seg inseg[0];
};

/*  TBD: can consider change to use above mlx5_wqe_mkey_context_seg upstream format */
struct mlx5_mkey_seg {
	/* This is a two bit field occupying bits 31-30.
	 * bit 31 is always 0,
	 * bit 30 is zero for regular MRs and 1 (e.g free) for UMRs that do not have tanslation
	 */
	uint8_t		status;
	uint8_t		pcie_control;
	uint8_t		flags;
	uint8_t		version;
	uint32_t	qpn_mkey7_0;
	uint8_t		rsvd1[4];
	uint32_t	flags_pd;
	uint64_t	start_addr;
	uint64_t	len;
	uint32_t	bsfs_octo_size;
	uint8_t		rsvd2[16];
	uint32_t	xlt_oct_size;
	uint8_t		rsvd3[3];
	uint8_t		log2_page_size;
	uint8_t		rsvd4[4];
};

struct mlx5_seg_set_psv {
	uint8_t		rsvd[4];
	uint16_t	syndrome;
	uint16_t	status;
	uint16_t	block_guard;
	uint16_t	app_tag;
	uint32_t	ref_tag;
	uint32_t	mkey;
	uint64_t	va;
};

struct mlx5_seg_get_psv {
	uint8_t		rsvd[19];
	uint8_t		num_psv;
	uint32_t	l_key;
	uint64_t	va;
	uint32_t	psv_index[4];
};

struct mlx5_seg_check_psv {
	uint8_t		rsvd0[2];
	uint16_t	err_coalescing_op;
	uint8_t		rsvd1[2];
	uint16_t	xport_err_op;
	uint8_t		rsvd2[2];
	uint16_t	xport_err_mask;
	uint8_t		rsvd3[7];
	uint8_t		num_psv;
	uint32_t	l_key;
	uint64_t	va;
	uint32_t	psv_index[4];
};

struct mlx5_klm {
	uint32_t	byte_count;
	uint32_t	key;
	uint64_t	va;
};

struct mlx5_seg_repeat_ent {
	uint16_t	stride;
	uint16_t	byte_count;
	uint32_t	memkey;
	uint64_t	va;
};

struct mlx5_seg_repeat_block {
	uint32_t			byte_count;
	uint32_t			const_0x400;
	uint32_t			repeat_count;
	uint16_t			reserved;
	uint16_t			num_ent;
	struct mlx5_seg_repeat_ent	entries[0];
};

struct mlx5_rwqe_sig {
	uint8_t		rsvd0[4];
	uint8_t		signature;
	uint8_t		rsvd1[11];
};

struct mlx5_wqe_signature_seg {
	uint8_t		rsvd0[4];
	uint8_t		signature;
	uint8_t		rsvd1[11];
};

struct mlx5_wqe_inline_seg {
	uint32_t	byte_count;
};

struct mlx5_wqe_wait_en_seg {
	uint8_t		rsvd0[8];
	uint32_t	pi;
	uint32_t	obj_num;
};

enum {
	MLX5_MKEY_MASK_LEN		= 1ull << 0,
	MLX5_MKEY_MASK_PAGE_SIZE	= 1ull << 1,
	MLX5_MKEY_MASK_START_ADDR	= 1ull << 6,
	MLX5_MKEY_MASK_PD		= 1ull << 7,
	MLX5_MKEY_MASK_EN_RINVAL	= 1ull << 8,
	MLX5_MKEY_MASK_EN_SIGERR	= 1ull << 9,
	MLX5_MKEY_MASK_BSF_EN		= 1ull << 12,
	MLX5_MKEY_MASK_KEY		= 1ull << 13,
	MLX5_MKEY_MASK_QPN		= 1ull << 14,
	MLX5_MKEY_MASK_ACCESS_MODE	= 1ull << 16,
	MLX5_MKEY_MASK_LR		= 1ull << 17,
	MLX5_MKEY_MASK_LW		= 1ull << 18,
	MLX5_MKEY_MASK_RR		= 1ull << 19,
	MLX5_MKEY_MASK_RW		= 1ull << 20,
	MLX5_MKEY_MASK_A		= 1ull << 21,
	MLX5_MKEY_MASK_SMALL_FENCE	= 1ull << 23,
	MLX5_MKEY_MASK_FREE		= 1ull << 29,
};

enum {
	MLX5_PERM_LOCAL_READ	= 1 << 2,
	MLX5_PERM_LOCAL_WRITE	= 1 << 3,
	MLX5_PERM_REMOTE_READ	= 1 << 4,
	MLX5_PERM_REMOTE_WRITE	= 1 << 5,
	MLX5_PERM_ATOMIC	= 1 << 6,
	MLX5_PERM_UMR_EN	= 1 << 7,
};

enum {
	MLX5_ACCESS_MODE_KLM	= 2,
};

enum {
	MLX5_CALC_OP_XOR = 0x5,
};

enum {
	MLX5_CALC_MATRIX = (1 << 7),
	MLX5_CALC_MATRIX_8BIT = (1 << 4),
};

struct mlx5_vec_calc_seg {
	uint8_t		calc_op[4];
	uint32_t	rsvd1[2];
	uint8_t		op_tags;
	uint8_t		mat_le_tag_cs;
	uint8_t		rsvd2;
	uint8_t		vec_count;
	uint32_t	rsvd3;
	uint32_t	cm_lkey;
	uint64_t	cm_addr;
	uint32_t	vec_size;
	uint32_t	vec_lkey;
	uint64_t	vec_addr;
};

#endif /* WQE_H */
