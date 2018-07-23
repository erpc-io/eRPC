/*
 * Copyright (c) 2006, 2007 Cisco, Inc.  All rights reserved.
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

#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>

#include "mlx4.h"
#include "bitmap.h"

struct mlx4_hugetlb_mem {
	int			shmid;
	char		       *shmaddr;
	struct mlx4_bitmap	bitmap;
	struct list_head	list;
};

#if !(defined(HAVE_IBV_DONTFORK_RANGE) && defined(HAVE_IBV_DOFORK_RANGE))

/*
 * If libibverbs isn't exporting these functions, then there's no
 * point in doing it here, because the rest of libibverbs isn't going
 * to be fork-safe anyway.
 */
static int ibv_dontfork_range(void *base, size_t size)
{
	return 0;
}

static int ibv_dofork_range(void *base, size_t size)
{
	return 0;
}

#endif /* HAVE_IBV_DONTFORK_RANGE && HAVE_IBV_DOFORK_RANGE */

void mlx4_hugetlb_mem_free(struct mlx4_hugetlb_mem *hmem)
{
	mlx4_bitmap_cleanup(&hmem->bitmap);

	if (shmdt((const void *)hmem->shmaddr) != 0) {
		if (mlx4_trace)			
			perror("Detach shm failure");
	}
	free(hmem);
}
static void mlx4_free_buf_huge_ex(struct mlx4_context *mctx,
					struct mlx4_buf *buf,
					int do_fork)
{
	struct mlx4_hugetlb_mem *hmem;

	if (do_fork)
		ibv_dofork_range(buf->buf, buf->length);

	if (buf->hmem == NULL) {
		if (mlx4_trace)
			perror("No hugetlb mem");
		return;
	}

	hmem = (struct mlx4_hugetlb_mem *) buf->hmem;
	mlx4_spin_lock(&mctx->hugetlb_lock);
	mlx4_bitmap_free_range(&hmem->bitmap, buf->base,
			       buf->length/MLX4_Q_CHUNK_SIZE);

	if (is_bitmap_empty(&hmem->bitmap)) {
		list_del(&hmem->list);
		mlx4_hugetlb_mem_free(hmem);
	}
	mlx4_spin_unlock(&mctx->hugetlb_lock);
}

void mlx4_free_buf_huge(struct mlx4_context *mctx, struct mlx4_buf *buf)
{
	mlx4_free_buf_huge_ex(mctx, buf, 1);
}

struct mlx4_hugetlb_mem *mxl4_hugetlb_mem_alloc(size_t size)
{
	struct mlx4_hugetlb_mem *hmem;
	size_t shm_len;

	hmem = malloc(sizeof(*hmem));
	if (!hmem)
		return NULL;
	
	shm_len = (size > MLX4_SHM_LENGTH) ? align(size, MLX4_SHM_LENGTH) :
			MLX4_SHM_LENGTH;
	hmem->shmid = shmget(IPC_PRIVATE, shm_len,
			     SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);
	if (hmem->shmid < 0) {
		if (mlx4_trace)
			perror("shmget");
		free(hmem);
		return NULL;
	}

	hmem->shmaddr = shmat(hmem->shmid, MLX4_SHM_ADDR, MLX4_SHMAT_FLAGS);
	if (hmem->shmaddr == (char *)-1) {
		if (mlx4_trace)
			perror("Shared memory attach failure");
		shmctl(hmem->shmid, IPC_RMID, NULL);
		free(hmem);
		return NULL;
	}

	if (mlx4_bitmap_init(&hmem->bitmap, shm_len/MLX4_Q_CHUNK_SIZE,
			 shm_len/MLX4_Q_CHUNK_SIZE - 1)) {
		if (mlx4_trace)
			perror("mlx4_bitmap_init");
		mlx4_hugetlb_mem_free(hmem);
		return NULL;
	}

	/* Marked to destroy when process detaches from shmget segment */
	shmctl(hmem->shmid, IPC_RMID, NULL);

	return hmem;
}


int mlx4_alloc_prefered_buf(struct mlx4_context *mctx,
				struct mlx4_buf *buf,
				size_t size, int page_size,
				enum mlx4_alloc_type alloc_type,
				const char *component)
{
	int ret = 1;

	buf->hmem = NULL;
	/* Fallback mechanism is used below:
	    priority is: huge pages , contig pages, default allocation */
	if (alloc_type == MLX4_ALLOC_TYPE_HUGE ||
		alloc_type == MLX4_ALLOC_TYPE_PREFER_HUGE ||
		alloc_type == MLX4_ALLOC_TYPE_ALL) {
		ret = mlx4_alloc_buf_huge(mctx, buf,
					  size,
					  page_size);
		if (!ret)
			return 0;

		/* Checking whether HUGE is forced */
		if (alloc_type == MLX4_ALLOC_TYPE_HUGE)
			return -1;
		if (mlx4_trace)
			printf(PFX "Huge mode allocation has failed,fallback to %s mode\n",
				MLX4_ALLOC_TYPE_ALL ? "contig" : "default");
			
	}

	if (alloc_type == MLX4_ALLOC_TYPE_CONTIG ||
		alloc_type == MLX4_ALLOC_TYPE_PREFER_CONTIG ||
		alloc_type == MLX4_ALLOC_TYPE_ALL) {
		ret = mlx4_alloc_buf_contig(mctx, buf,
					  size,
					  page_size,
					  component, NULL);
		if (!ret)
			return 0;

		/* Checking whether CONTIG is forced */
		if (alloc_type == MLX4_ALLOC_TYPE_CONTIG)
			return -1;
		if (mlx4_trace)
			printf(PFX "Contig mode allocation has failed,fallback to default mode\n");				
	}

	return mlx4_alloc_buf(buf, size, page_size);

}


int mlx4_alloc_buf(struct mlx4_buf *buf, size_t size, int page_size)
{
	int ret;

	buf->length = align(size, page_size);
	buf->buf = mmap(NULL, buf->length, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf->buf == MAP_FAILED)
		return errno;

	ret = ibv_dontfork_range(buf->buf, size);
	if (ret)
		munmap(buf->buf, buf->length);

	return ret;
}

void mlx4_free_buf(struct mlx4_buf *buf)
{
	if (buf->length) {
		ibv_dofork_range(buf->buf, buf->length);
		munmap(buf->buf, buf->length);
	}
}

/* This function computes log2(v) rounded up.
*   We don't want to have a dependency to libm which exposes ceil & log2 APIs.
*   Code was written based on public domain code:
	URL: http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog.
*/
static uint32_t mlx4_get_block_order(uint32_t v)
{
	static const uint32_t bits_arr[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
	static const uint32_t shift_arr[] = {1, 2, 4, 8, 16};
	int i;
	uint32_t input_val = v;

	register uint32_t r = 0;/* result of log2(v) will go here */
	for (i = 4; i >= 0; i--) {

		if (v & bits_arr[i]) {
			v >>= shift_arr[i];
			r |= shift_arr[i];
		}
	}
	/* Rounding up if required */
	r += !!(input_val & ((1 << r) - 1));

	return r;
}


static int mlx4_finalize_contiguous_alloc(struct mlx4_buf *buf,
						void *addr,
						size_t length)
{
	if (ibv_dontfork_range(addr, length)) {
		munmap(addr, length);
		return 1;
	}

	/* We hook addr & length also internally for further
	     use via dreg_mr. On ibv_mr returned to user length or address may
	     be different than the allocated length or address as of alignment
	     issues.
	*/
	buf->buf = addr;
	buf->length = length;
	return 0;

}


void mlx4_get_alloc_type(struct ibv_context *context, const char *component,
			 enum mlx4_alloc_type *alloc_type,
			 enum mlx4_alloc_type default_alloc_type)

{
	char env_value[VERBS_MAX_ENV_VAL];
	char name_buff[128];

	sprintf(name_buff, "%s_ALLOC_TYPE", component);

	/* First set defaults */
	*alloc_type = default_alloc_type;

	if (!ibv_exp_cmd_getenv(context, name_buff, env_value, sizeof(env_value))) {
		if (!strcasecmp(env_value, "ANON"))
			*alloc_type = MLX4_ALLOC_TYPE_ANON;
		else if (!strcasecmp(env_value, "HUGE"))
			*alloc_type = MLX4_ALLOC_TYPE_HUGE;
		else if (!strcasecmp(env_value, "CONTIG"))
			*alloc_type = MLX4_ALLOC_TYPE_CONTIG;
		else if (!strcasecmp(env_value, "PREFER_CONTIG"))
			*alloc_type = MLX4_ALLOC_TYPE_PREFER_CONTIG;
		else if (!strcasecmp(env_value, "PREFER_HUGE"))
			*alloc_type = MLX4_ALLOC_TYPE_PREFER_HUGE;
		else if (!strcasecmp(env_value, "ALL"))
			*alloc_type = MLX4_ALLOC_TYPE_ALL;
	}

	return;
}


static void mlx4_alloc_get_env_info(struct ibv_context *context,
				    int *max_log2_contig_block_size,
				    int *min_log2_contig_block_size,
				    const char *component)

{
	char env_value[VERBS_MAX_ENV_VAL];
	int value;
	char name_buff[128];

	/* First set defaults */
	*max_log2_contig_block_size = MLX4_MAX_LOG2_CONTIG_BLOCK_SIZE;
	*min_log2_contig_block_size = MLX4_MIN_LOG2_CONTIG_BLOCK_SIZE;

	sprintf(name_buff, "%s_MAX_LOG2_CONTIG_BSIZE", component);
	if (!ibv_exp_cmd_getenv(context, name_buff, env_value, sizeof(env_value))) {
		value = atoi(env_value);
		if (value <= MLX4_MAX_LOG2_CONTIG_BLOCK_SIZE &&
		    value >= MLX4_MIN_LOG2_CONTIG_BLOCK_SIZE)
			*max_log2_contig_block_size = value;
		else
			fprintf(stderr,
			"Invalid value %d for %s\n",
				value, name_buff);
	}
	sprintf(name_buff, "%s_MIN_LOG2_CONTIG_BSIZE", component);
	if (!ibv_exp_cmd_getenv(context, name_buff, env_value, sizeof(env_value))) {
		value = atoi(env_value);
		if (value >= MLX4_MIN_LOG2_CONTIG_BLOCK_SIZE &&
		    value  <=  *max_log2_contig_block_size)
			*min_log2_contig_block_size = value;
		else
			fprintf(stderr,
			"Invalid value %d for %s\n",
				value, name_buff);
	}
	return;
}



int mlx4_alloc_buf_contig(struct mlx4_context *mctx,
				struct mlx4_buf *buf, size_t size,
				int page_size,
				const char *component, void *req_addr)
{
	void *addr = NULL;
	int block_size_exp;
	int max_log2_contig_block_size;
	int min_log2_contig_block_size;
	int mmap_flags = MAP_SHARED;
	void *act_addr = NULL;
	size_t act_size = size;

	struct ibv_context *context = &(mctx->ibv_ctx);

	mlx4_alloc_get_env_info(&mctx->ibv_ctx,
				&max_log2_contig_block_size,
				&min_log2_contig_block_size,
				component);

	/* Checking that we don't pass max block size */
	if (size >= (1 << max_log2_contig_block_size))
		block_size_exp = max_log2_contig_block_size;
	else
		block_size_exp = mlx4_get_block_order(size);

	if (req_addr) {
		act_addr = (void *)((uintptr_t)req_addr & ~((uintptr_t)page_size - 1));
		act_size += (size_t)((uintptr_t)req_addr - (uintptr_t)act_addr);
		mmap_flags |= MAP_FIXED;
	}

	do {
		/* The second parameter holds the total required length for
		     this contiguous allocation aligned to page size.
		     When calling mmap the last offset parameter
		     should be a multiple of the page size and holds:
		     1) Indication that we are in that mode of
			allocation contiguous memory (value #2)
		     2) The required size of each block.
			To enable future actions on mmap we
			use the last 3 bits of the offset parameter
			as the command identifier.
		*/
		addr = mmap(act_addr, act_size,
				PROT_WRITE | PROT_READ, mmap_flags,
				context->cmd_fd,
				page_size *
				(MLX4_MMAP_GET_CONTIGUOUS_PAGES_CMD +
				(block_size_exp << MLX4_MMAP_CMD_BITS)));

		/* On a failure  MAP_FAILED (that is, (void *) -1) is returned*/
		if (addr != MAP_FAILED)
			break;

		/* We failed - set addr to NULL and checks whether
		     a retry is relevant.
		* If kernel doesn't support this command as of
		   compatibility issues we'll also get EINVAL.
		*/
		addr = NULL;
		if (errno == EINVAL)
			break;

		/* Retring asking for less contiguous pages per block */
		block_size_exp -= 1;
	} while (block_size_exp >= min_log2_contig_block_size);

	if (!addr)
		return 1;

	/* All was ok we'll make final steps to have this addr ready*/
	return mlx4_finalize_contiguous_alloc(buf, addr, act_size);
}

int mlx4_alloc_buf_huge(struct mlx4_context *mctx, struct mlx4_buf *buf,
			size_t size, int page_size)
{
	struct mlx4_hugetlb_mem *hmem, *tmp_hmem;
	int found = 0;
	int ret = 0;
	LIST_HEAD(slist);

	buf->length = align(size, MLX4_Q_CHUNK_SIZE);

	mlx4_spin_lock(&mctx->hugetlb_lock);
	list_for_each_entry_safe(hmem, tmp_hmem, &mctx->hugetlb_list, list) {
		if (is_bitmap_avail(&hmem->bitmap)) {
			buf->base = mlx4_bitmap_alloc_range(&hmem->bitmap,
					buf->length/MLX4_Q_CHUNK_SIZE, 1);
			if (buf->base == -1)
				continue;
			else {
				buf->hmem = (void *)hmem;
				found = 1;
				break;
			}
		}
	}
	mlx4_spin_unlock(&mctx->hugetlb_lock);

	if (!found) {
		int avail;

		hmem = mxl4_hugetlb_mem_alloc(buf->length);
		if (hmem == NULL)			
			return -1;

		buf->base = mlx4_bitmap_alloc_range(&hmem->bitmap,
					buf->length/MLX4_Q_CHUNK_SIZE, 1);
		if (buf->base == -1) {
			if (mlx4_trace)
				perror("mlx4_bitmap_alloc_range");
			mlx4_hugetlb_mem_free(hmem);
			return -1;
		}

		buf->hmem = (void *)hmem;

		avail = is_bitmap_avail(&hmem->bitmap);
		mlx4_spin_lock(&mctx->hugetlb_lock);
		if (avail)
			list_add(&hmem->list, &mctx->hugetlb_list);
		else
			list_add_tail(&hmem->list, &mctx->hugetlb_list);
		mlx4_spin_unlock(&mctx->hugetlb_lock);
	}

	buf->buf = hmem->shmaddr + (buf->base * MLX4_Q_CHUNK_SIZE);

	ret = ibv_dontfork_range(buf->buf, buf->length);
	if (ret) {
		mlx4_free_buf_huge_ex(mctx, buf, 0);
		buf->hmem = NULL;
		if (mlx4_trace)
			perror("ibv_dontfork_range");
	}

	return ret;
}

