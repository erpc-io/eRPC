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


#ifndef DOORBELL_H
#define DOORBELL_H

#if SIZEOF_LONG == 8

#if __BYTE_ORDER == __LITTLE_ENDIAN
#  define MLX5_PAIR_TO_64(val) ((uint64_t) val[1] << 32 | val[0])
#elif __BYTE_ORDER == __BIG_ENDIAN
#  define MLX5_PAIR_TO_64(val) ((uint64_t) val[0] << 32 | val[1])
#else
#  error __BYTE_ORDER not defined
#endif

static inline void mlx5_write64(uint32_t val[2],
				void *dest,
				struct mlx5_lock *lock)
{
	*(volatile uint64_t *)dest = MLX5_PAIR_TO_64(val);
}

#else

static inline void mlx5_write64(uint32_t val[2],
				void *dest,
				struct mlx5_lock *lock)
{
	mlx5_lock(lock);
	*(volatile uint32_t *)dest		= val[0];
	*(volatile uint32_t *)(dest + 4)	= val[1];
	mlx5_unlock(lock);
}

#endif

/*
 * Avoid using memcpy() to copy to BlueFlame page, since memcpy()
 * implementations may use move-string-buffer assembler instructions,
 * which do not guarantee order of copying.
 */
#if defined(__x86_64__)
#define COPY_64B_NT(dst, src)		\
	__asm__ __volatile__ (		\
	" movdqa   (%1),%%xmm0\n"	\
	" movdqa 16(%1),%%xmm1\n"	\
	" movdqa 32(%1),%%xmm2\n"	\
	" movdqa 48(%1),%%xmm3\n"	\
	" movntdq %%xmm0,   (%0)\n"	\
	" movntdq %%xmm1, 16(%0)\n"	\
	" movntdq %%xmm2, 32(%0)\n"	\
	" movntdq %%xmm3, 48(%0)\n"	\
	: : "r" (dst), "r" (src) : "memory");	\
	dst += 8;			\
	src += 8
#else
#define COPY_64B_NT(dst, src)	\
	*dst++ = *src++;	\
	*dst++ = *src++;	\
	*dst++ = *src++;	\
	*dst++ = *src++;	\
	*dst++ = *src++;	\
	*dst++ = *src++;	\
	*dst++ = *src++;	\
	*dst++ = *src++

#endif

#if defined(__aarch64__)
static void mlx5_bf_copy(unsigned long long *dst, unsigned long long *src,
			 unsigned bytecnt, struct mlx5_qp *qp)
{
	volatile __uint128_t *to = (volatile __uint128_t *)dst;
	__uint128_t *from = (__uint128_t *)src;
	__uint128_t dw0, dw1, dw2, dw3;

	while (bytecnt > 0) {
		dw0 = *from++;
		dw1 = *from++;
		dw2 = *from++;
		dw3 = *from++;
		*to++ = dw0;
		*to++ = dw1;
		*to++ = dw2;
		*to++ = dw3;
		bytecnt -= 4 * sizeof(__uint128_t);
		if (unlikely(from == qp->gen_data.sqend))
			from = (__uint128_t *)(qp->gen_data.sqstart);
	}
}
#else
static void mlx5_bf_copy(unsigned long long *dst, unsigned long long *src,
			 unsigned bytecnt, struct mlx5_qp *qp)
{
	while (bytecnt > 0) {
		COPY_64B_NT(dst, src);
		bytecnt -= 8 * sizeof(unsigned long long);
		if (unlikely(src == qp->gen_data.sqend))
			src = qp->gen_data.sqstart;
	}
}
#endif

static inline void mlx5_write_db(unsigned long long *dst, unsigned long long *src)
{
	*dst = *src;
}

static inline int __ring_db(struct mlx5_qp *qp, const int db_method, uint32_t curr_post, unsigned long long *seg, int size) __attribute__((always_inline));
static inline int __ring_db(struct mlx5_qp *qp, const int db_method, uint32_t curr_post, unsigned long long *seg, int size)
{
	struct mlx5_bf *bf = qp->gen_data.bf;

	qp->gen_data.last_post = curr_post;
	qp->mpw.state = MLX5_MPW_STATE_CLOSED;

	switch (db_method) {
	case MLX5_DB_METHOD_DEDIC_BF_1_THREAD:
		/* This QP is used by one thread and it uses dedicated blue-flame */

		/* Use wc_wmb to make sure old BF-copy is not passing current DB record */
		wc_wmb();
		qp->gen_data.db[MLX5_SND_DBR] = htonl(curr_post);

		/* This wc_wmb ensures ordering between DB record and BF copy */
		wc_wmb();
		if (size <= bf->buf_size / 64) {
			mlx5_bf_copy(bf->reg + bf->offset, seg,
				     size * 64, qp);

			/* No need for wc_wmb since cpu arch support auto WC buffer eviction */
		} else {
			mlx5_write_db(bf->reg + bf->offset, seg);
			wc_wmb();
		}
		bf->offset ^= bf->buf_size;
		break;

	case MLX5_DB_METHOD_DEDIC_BF:
		/* The QP has dedicated blue-flame */

		/*
		 * Make sure that descriptors are written before
		 * updating doorbell record and ringing the doorbell
		 */
		wmb();
		qp->gen_data.db[MLX5_SND_DBR] = htonl(curr_post);

		/* This wc_wmb ensures ordering between DB record and BF copy */
		wc_wmb();
		if (size <= bf->buf_size / 64)
			mlx5_bf_copy(bf->reg + bf->offset, seg,
				     size * 64, qp);
		else
			mlx5_write_db(bf->reg + bf->offset, seg);
		/*
		 * use wc_wmb to ensure write combining buffers are flushed out
		 * of the running CPU. This must be carried inside the spinlock.
		 * Otherwise, there is a potential race. In the race, CPU A
		 * writes doorbell 1, which is waiting in the WC buffer. CPU B
		 * writes doorbell 2, and it's write is flushed earlier. Since
		 * the wc_wmb is CPU local, this will result in the HCA seeing
		 * doorbell 2, followed by doorbell 1.
		 */
		wc_wmb();
		bf->offset ^= bf->buf_size;
		break;

	case MLX5_DB_METHOD_BF:
		/* The QP has blue-flame that may be shared by other QPs */

		/*
		 * Make sure that descriptors are written before
		 * updating doorbell record and ringing the doorbell
		 */
		wmb();
		qp->gen_data.db[MLX5_SND_DBR] = htonl(curr_post);

		/* This wc_wmb ensures ordering between DB record and BF copy */
		wc_wmb();
		mlx5_lock(&bf->lock);
		if (size <= bf->buf_size / 64)
			mlx5_bf_copy(bf->reg + bf->offset, seg,
				     size * 64, qp);
		else
			mlx5_write_db(bf->reg + bf->offset, seg);
		/*
		 * use wc_wmb to ensure write combining buffers are flushed out
		 * of the running CPU. This must be carried inside the spinlock.
		 * Otherwise, there is a potential race. In the race, CPU A
		 * writes doorbell 1, which is waiting in the WC buffer. CPU B
		 * writes doorbell 2, and it's write is flushed earlier. Since
		 * the wc_wmb is CPU local, this will result in the HCA seeing
		 * doorbell 2, followed by doorbell 1.
		 */
		wc_wmb();
		bf->offset ^= bf->buf_size;
		mlx5_unlock(&bf->lock);
		break;

	case MLX5_DB_METHOD_DB:
		/* doorbell mapped to non-cached memory */

		/*
		 * Make sure that descriptors are written before
		 * updating doorbell record and ringing the doorbell
		 */
		wmb();
		qp->gen_data.db[MLX5_SND_DBR] = htonl(curr_post);

		/* This wmb ensures ordering between DB record and DB ringing */
		wmb();
		mlx5_write64((__be32 *)seg, bf->reg + bf->offset, &bf->lock);
		break;
	}

	return 0;
}

#endif /* DOORBELL_H */
