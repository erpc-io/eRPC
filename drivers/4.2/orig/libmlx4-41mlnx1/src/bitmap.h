/*
 * Copyright (c) 2000, 2011 Mellanox Technology Inc.  All rights reserved.
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

#ifndef BITMAP_H
#define BITMAP_H

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <linux/errno.h>

#ifndef min
#define min(a, b)		\
	({ typeof(a) _a = (a);	\
	   typeof(b) _b = (b);	\
	   _a < _b ? _a : _b; })
#endif

/* Only ia64 requires this */
#ifdef __ia64__
#define MLX4_SHM_ADDR (void *)(0x8000000000000000UL)
#define MLX4_SHMAT_FLAGS (SHM_RND)
#else
#define MLX4_SHM_ADDR (void *)(0x0UL)
#define MLX4_SHMAT_FLAGS (0)
#endif

struct __dummy_h { unsigned long a[100]; };
#define MLX4_ADDR (*(struct __dummy_h *) addr)
#define MLX4_CONST_ADDR (*(const struct __dummy_h *) addr)

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define BITS_PER_BYTE		8
#define BITS_PER_WORD		(BITS_PER_BYTE * sizeof(uint32_t))
#define BITS_TO_WORDS(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(uint32_t))

#ifndef HPAGE_SIZE
#define HPAGE_SIZE		(2UL*1024*1024)
#endif

#define MLX4_SHM_LENGTH		(HPAGE_SIZE)
#define MLX4_Q_CHUNK_SIZE	32768
#define MLX4_SHM_NUM_REGION	64

struct mlx4_bitmap {
	uint32_t		last;
	uint32_t		top;
	uint32_t		max;
	uint32_t		avail;
	uint32_t		mask;
	struct mlx4_spinlock	lock;
	uint32_t		*table;
};

inline unsigned long mlx4_ffz(uint32_t word)
{
	return __builtin_ffs(~word) - 1;
}

inline void mlx4_set_bit(unsigned int nr, uint32_t *addr)
{

	addr[(nr / BITS_PER_WORD)]
	|= (1 << (nr % BITS_PER_WORD));


}

inline void mlx4_clear_bit(unsigned int nr,  uint32_t *addr)
{
	addr[(nr / BITS_PER_WORD)]
		&= ~(1 << (nr % BITS_PER_WORD));
}

inline int mlx4_test_bit(unsigned int nr, const uint32_t *addr)
{
	return !!(addr[(nr / BITS_PER_WORD)]
		& (1 <<  (nr % BITS_PER_WORD)));
}

inline uint32_t mlx4_find_first_zero_bit(const uint32_t *addr,
						uint32_t size)
{
	const uint32_t *p = addr;
	uint32_t result = 0;
	uint32_t tmp;

	while (size & ~(BITS_PER_WORD - 1)) {
		tmp = *(p++);
		if (~tmp)
			goto found;
		result += BITS_PER_WORD;
		size -= BITS_PER_WORD;
	}
	if (!size)
		return result;

	tmp = (*p) | (~0UL << size);
	if (tmp == (uint32_t)~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found:
	return result + mlx4_ffz(tmp);
}

int mlx4_bitmap_alloc(struct mlx4_bitmap *bitmap)
{
	uint32_t obj;
	int ret;

	mlx4_spin_lock(&bitmap->lock);

	obj = mlx4_find_first_zero_bit(bitmap->table, bitmap->max);
	if (obj < bitmap->max) {
		mlx4_set_bit(obj, bitmap->table);
		bitmap->last = (obj + 1);
		if (bitmap->last == bitmap->max)
			bitmap->last = 0;
		obj |= bitmap->top;
		ret = obj;
	} else
		ret = -1;

	if (ret != -1)
		--bitmap->avail;

	mlx4_spin_unlock(&bitmap->lock);

	return ret;
}

static inline uint32_t find_aligned_range(uint32_t *bitmap,
					uint32_t start, uint32_t nbits,
					int len, int alignment)
{
	uint32_t end, i;

again:
	start = align(start, alignment);

	while ((start < nbits) && mlx4_test_bit(start, bitmap))
		start += alignment;

	if (start >= nbits)
		return -1;

	end = start + len;
	if (end > nbits)
		return -1;

	for (i = start + 1; i < end; i++) {
		if (mlx4_test_bit(i, bitmap)) {
			start = i + 1;
			goto again;
		}
	}

	return start;
}

static inline int mlx4_bitmap_alloc_range(struct mlx4_bitmap *bitmap, int cnt,
					int align)
{
	uint32_t obj;
	int ret, i;

	if (cnt == 1 && align == 1)
		return mlx4_bitmap_alloc(bitmap);

	if (cnt > bitmap->max)
		return -1;

	mlx4_spin_lock(&bitmap->lock);

	obj = find_aligned_range(bitmap->table, bitmap->last,
				 bitmap->max, cnt, align);
	if (obj >= bitmap->max) {
		bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
		obj = find_aligned_range(bitmap->table, 0, bitmap->max,
					 cnt, align);
	}

	if (obj < bitmap->max) {
		for (i = 0; i < cnt; i++)
			mlx4_set_bit(obj + i, bitmap->table);
		if (obj == bitmap->last) {
			bitmap->last = (obj + cnt);
			if (bitmap->last >= bitmap->max)
				bitmap->last = 0;
		}
		obj |= bitmap->top;
		ret = obj;
	} else
		ret = -1;

	if (ret != -1)
		bitmap->avail -= cnt;

	mlx4_spin_unlock(&bitmap->lock);

	return obj;
}

static inline void mlx4_bitmap_free_range(struct mlx4_bitmap *bitmap, uint32_t obj,
					int cnt)
{
	int i;

	obj &= bitmap->max - 1;

	mlx4_spin_lock(&bitmap->lock);
	for (i = 0; i < cnt; i++)
		mlx4_clear_bit(obj + i, bitmap->table);
	bitmap->last = min(bitmap->last, obj);
	bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
	bitmap->avail += cnt;
	mlx4_spin_unlock(&bitmap->lock);
}

static inline int is_bitmap_empty(struct mlx4_bitmap *bitmap)
{
	int ret;

	mlx4_spin_lock(&bitmap->lock);
	ret = (bitmap->avail == bitmap->max) ? 1 : 0;
	mlx4_spin_unlock(&bitmap->lock);

	return ret;
}

static inline int is_bitmap_avail(struct mlx4_bitmap *bitmap)
{
	int ret;

	mlx4_spin_lock(&bitmap->lock);
	ret = (bitmap->avail > 0) ? 1 : 0;
	mlx4_spin_unlock(&bitmap->lock);

	return ret;
}

int mlx4_bitmap_init(struct mlx4_bitmap *bitmap, uint32_t num, uint32_t mask)
{
	bitmap->last = 0;
	bitmap->top  = 0;
	bitmap->max  = bitmap->avail = num;
	bitmap->mask = mask;
	bitmap->avail = bitmap->max;
	mlx4_spinlock_init(&bitmap->lock, !mlx4_single_threaded);
	bitmap->table = malloc(BITS_TO_WORDS(bitmap->max) * sizeof(uint32_t));

	if (!bitmap->table)
		return -ENOMEM;
	memset((void *)bitmap->table, 0,
		(int)(BITS_TO_WORDS(bitmap->max) * sizeof(uint32_t)));
	return 0;
}

inline void mlx4_bitmap_cleanup(struct mlx4_bitmap *bitmap)
{
	if (bitmap->table)
		free(bitmap->table);
}

static inline void mlx4_bitmap_free(struct mlx4_bitmap *bitmap, uint32_t obj)
{
	mlx4_bitmap_free_range(bitmap, obj, 1);
}

#endif
