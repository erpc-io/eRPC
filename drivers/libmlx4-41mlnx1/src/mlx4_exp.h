/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
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

#ifndef MLX4_EXP_H
#define MLX4_EXP_H

#include <infiniband/kern-abi_exp.h>
#include "mlx4.h"

/*
 * mlx4-abi experimental structs
 */
struct mlx4_exp_create_qp {
	struct ibv_exp_create_qp		ibv_cmd;
	struct mlx4_exp_create_qp_provider	exp_cmd;
};

struct mlx4_exp_create_cq {
	struct ibv_exp_create_cq	ibv_cmd;
	__u64				buf_addr;
	__u64				db_addr;
};

/*
 * Experimental functions
 */
struct ibv_qp *mlx4_exp_create_qp(struct ibv_context *context,
				  struct ibv_exp_qp_init_attr *attr);
int mlx4_exp_query_device(struct ibv_context *context,
			  struct ibv_exp_device_attr *attr);
int mlx4_exp_query_port(struct ibv_context *context, uint8_t port_num,
			struct ibv_exp_port_attr *port_attr);
int mlx4_exp_modify_cq(struct ibv_cq *cq, struct ibv_exp_cq_attr *attr,
		       int attr_mask);
int mlx4_exp_dereg_mr(struct ibv_mr *mr, struct ibv_exp_dereg_out *out);
struct ibv_exp_res_domain *mlx4_exp_create_res_domain(struct ibv_context *context,
						      struct ibv_exp_res_domain_init_attr *attr);
int mlx4_exp_destroy_res_domain(struct ibv_context *context,
				struct ibv_exp_res_domain *res_dom,
				struct ibv_exp_destroy_res_domain_attr *attr);
void *mlx4_exp_query_intf(struct ibv_context *context, struct ibv_exp_query_intf_params *params,
			  enum ibv_exp_query_intf_status *status);
int mlx4_exp_release_intf(struct ibv_context *context, void *intf,
			  struct ibv_exp_release_intf_params *params);

#endif /* MLX4_EXP_H */
