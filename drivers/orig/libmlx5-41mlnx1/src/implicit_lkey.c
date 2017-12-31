#include <pthread.h>
#include <infiniband/verbs.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include "implicit_lkey.h"
#include "mlx5.h"

#define LEVEL1_SIZE 10
#define LEVEL2_SIZE 11
#define MR_SIZE 28

#define ADDR_EFFECTIVE_BITS (MR_SIZE + LEVEL2_SIZE + LEVEL1_SIZE)

#define LEVEL1_SHIFT (MR_SIZE + LEVEL2_SIZE)
#define LEVEL2_SHIFT MR_SIZE

#define MASK(len) ((1 << (len)) - 1)

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

struct mlx5_implicit_lkey *mlx5_get_implicit_lkey(struct mlx5_pd *pd,
						  uint64_t exp_access)
{
	if (!(exp_access & IBV_EXP_ACCESS_ON_DEMAND)) {
		fprintf(stderr, "cannot create relaxed or implicit\
			 MR as a non-ODP MR\n");
		errno = EINVAL;
		return NULL;
	}

	if ((exp_access & ~IBV_EXP_ACCESS_RELAXED) == IBV_EXP_ACCESS_ON_DEMAND)
		return &pd->r_ilkey;

	if ((exp_access & ~IBV_EXP_ACCESS_RELAXED) ==
	    (IBV_EXP_ACCESS_ON_DEMAND | IBV_EXP_ACCESS_LOCAL_WRITE))
		return &pd->w_ilkey;

	if (!(exp_access & IBV_EXP_ACCESS_RELAXED)) {
		fprintf(stderr, "cannot create a strict MR (non-relaxed)\
			 for remote access\n");
		errno = EINVAL;
		return NULL;
	}

	if (!pd->remote_ilkey) {
		pd->remote_ilkey = malloc(sizeof(struct mlx5_implicit_lkey));
		if (!pd->remote_ilkey) {
			errno = ENOMEM;
			return NULL;
		}

		errno = mlx5_init_implicit_lkey(pd->remote_ilkey,
				IBV_EXP_ACCESS_LOCAL_WRITE |
				IBV_EXP_ACCESS_REMOTE_READ |
				IBV_EXP_ACCESS_REMOTE_WRITE |
				IBV_EXP_ACCESS_REMOTE_ATOMIC |
				IBV_EXP_ACCESS_ON_DEMAND);
		if (errno) {
			free(pd->remote_ilkey);
			pd->remote_ilkey = NULL;
		}
	}

	return pd->remote_ilkey;
}

int mlx5_init_implicit_lkey(struct mlx5_implicit_lkey *ilkey,
			    uint64_t exp_access)
{
	ilkey->table = NULL;
	ilkey->exp_access = exp_access;

	if (!(exp_access & IBV_EXP_ACCESS_ON_DEMAND))
		return -EINVAL;

	return pthread_mutex_init(&(ilkey->lock), NULL);
}

static void destroy_level2(struct mlx5_pair_mrs *table)
{
	struct mlx5_pair_mrs *ptr = table;
	for (; ptr != table + (1 << LEVEL2_SIZE); ++ptr) {
		if (ptr->mrs[0]) {
			to_mmr(ptr->mrs[0])->alloc_flags &= ~IBV_EXP_ACCESS_RELAXED;
			ibv_dereg_mr(ptr->mrs[0]);
		}
		if (ptr->mrs[1]) {
			to_mmr(ptr->mrs[1])->alloc_flags &= ~IBV_EXP_ACCESS_RELAXED;
			ibv_dereg_mr(ptr->mrs[1]);
		}
	}

	free(table);
}

void mlx5_destroy_implicit_lkey(struct mlx5_implicit_lkey *ilkey)
{
	struct mlx5_pair_mrs **ptr = ilkey->table;

	pthread_mutex_destroy(&ilkey->lock);

	if (ptr) {
		for (; ptr != ilkey->table + (1 << LEVEL1_SIZE); ++ptr)
			if (*ptr)
				destroy_level2(*ptr);

		free(ilkey->table);
	}
}

struct ibv_mr *mlx5_alloc_whole_addr_mr(const struct ibv_exp_reg_mr_in *attr)
{
	struct ibv_mr *mr;

	if (attr->exp_access & ~(IBV_EXP_ACCESS_ON_DEMAND |
				 IBV_EXP_ACCESS_LOCAL_WRITE))
		return NULL;

	mr = malloc(sizeof(struct ibv_mr));

	if (!mr)
		return NULL;

	mr->context = attr->pd->context;
	mr->pd = attr->pd;
	mr->addr = attr->addr;
	mr->length = attr->length;
	mr->handle = 0;
	mr->lkey = attr->exp_access & IBV_EXP_ACCESS_LOCAL_WRITE ?
			ODP_GLOBAL_W_LKEY : ODP_GLOBAL_R_LKEY;
	mr->rkey = 0;

	return mr;
}

void mlx5_dealloc_whole_addr_mr(struct ibv_mr *mr)
{
	free(mr);
}

int mlx5_get_real_mr_from_implicit_lkey(struct mlx5_pd *pd,
					struct mlx5_implicit_lkey *ilkey,
					uint64_t addr, uint64_t len,
					struct ibv_mr **mr)
{
	uint64_t key1 = (addr >> LEVEL1_SHIFT) & MASK(LEVEL1_SIZE);
	uint64_t key2 = (addr >> LEVEL2_SHIFT) & MASK(LEVEL2_SIZE);
	uint64_t addr_msb_bits = addr >> ADDR_EFFECTIVE_BITS;
	uint64_t mr_base_addr = addr & ~MASK(MR_SIZE);
	int mr_idx_in_pair = (((addr >> (MR_SIZE)) & 1) !=
			      (((addr+len+1) >> (MR_SIZE)) & 1));

	mr_base_addr |= (mr_idx_in_pair << (MR_SIZE-1));

	if (len >> MR_SIZE) {
		fprintf(stderr, "range too large for the implicit MR\n");
		return EINVAL;
	}

	/* Verify that the address is canonical, refuse posting a WQE
	 * for non-canonical addresses. To remove this limitation, add
	 * 5 levels to the tree here.
	 */
	if (addr_msb_bits &&
	    (addr_msb_bits != ((~((uint64_t)0)) >> ADDR_EFFECTIVE_BITS)))
		return EINVAL;


	/* Access the table in lock-free manner.
	 *
	 * As we only add items to the table, only lock it when adding
	 * the items, and check that the item is still missing with
	 * lock held. Assumes that writes to pointers are atomic, so
	 * we will never read "half-pointer".
	 */
	if (!ilkey->table) {
		pthread_mutex_lock(&ilkey->lock);
		if (!ilkey->table)
			ilkey->table = calloc(1, sizeof(void *) *
				       (1 << LEVEL1_SIZE));
		pthread_mutex_unlock(&ilkey->lock);
		if (!ilkey->table)
			return ENOMEM;
	}

	if (!ilkey->table[key1]) {
		pthread_mutex_lock(&ilkey->lock);
		if (!ilkey->table[key1])
			ilkey->table[key1] = calloc(1,
						    (sizeof(struct mlx5_pair_mrs) *
						    (1 << LEVEL2_SIZE)));
		pthread_mutex_unlock(&ilkey->lock);
		if (!ilkey->table[key1])
			return ENOMEM;
	}

	if (!ilkey->table[key1][key2].mrs[mr_idx_in_pair]) {
		pthread_mutex_lock(&ilkey->lock);
		if (!ilkey->table[key1][key2].mrs[mr_idx_in_pair]) {
			struct ibv_exp_reg_mr_in attr = {
				.comp_mask = 0,
				.pd = &pd->ibv_pd,
				.addr = (void *)(unsigned long)mr_base_addr,
				.length = 1 << MR_SIZE,
				.exp_access = ilkey->exp_access,
			};

			ilkey->table[key1][key2].mrs[mr_idx_in_pair] = ibv_exp_reg_mr(&attr);
			if (ilkey->table[key1][key2].mrs[mr_idx_in_pair]) {
				ilkey->table[key1][key2].mrs[mr_idx_in_pair]->addr = (void *)(unsigned long)mr_base_addr;
				ilkey->table[key1][key2].mrs[mr_idx_in_pair]->length = 1 << MR_SIZE;
			}
		}
		if (ilkey->table[key1][key2].mrs[mr_idx_in_pair]) {
			to_mmr(ilkey->table[key1][key2].mrs[mr_idx_in_pair])->alloc_flags |= IBV_EXP_ACCESS_RELAXED;
			to_mmr(ilkey->table[key1][key2].mrs[mr_idx_in_pair])->type = MLX5_ODP_MR;
		}
		pthread_mutex_unlock(&ilkey->lock);
		if (!ilkey->table[key1][key2].mrs[mr_idx_in_pair])
			return ENOMEM;
	}

	*mr = ilkey->table[key1][key2].mrs[mr_idx_in_pair];

	assert((*mr)->addr <= (void *)(unsigned long)addr &&
	       (void *)(unsigned long)addr + len <=
	       (*mr)->addr + (*mr)->length);
	return 0;
}

int mlx5_get_real_lkey_from_implicit_lkey(struct mlx5_pd *pd,
					  struct mlx5_implicit_lkey *ilkey,
					  uint64_t addr, size_t len,
					  uint32_t *lkey)
{
	struct ibv_mr *mr;
	int ret_val = mlx5_get_real_mr_from_implicit_lkey(pd, ilkey, addr,
							  len, &mr);

	if (ret_val == 0)
		*lkey = mr->lkey;
	return ret_val;
}

#define PREFETCH_STRIDE_SIZE (MASK(MR_SIZE-1))
int mlx5_prefetch_implicit_lkey(struct mlx5_pd *pd,
				struct mlx5_implicit_lkey *ilkey,
				uint64_t addr, size_t len, int flags)
{
	uint64_t end_addr = addr + len;
	if (addr > end_addr)
		return EINVAL;
	while (addr < end_addr) {
		struct ibv_mr *mr;
		struct ibv_exp_prefetch_attr attr;
		size_t effective_length = MIN(1+PREFETCH_STRIDE_SIZE -
					      (addr & PREFETCH_STRIDE_SIZE),
					      end_addr - addr);
		int ret_val = mlx5_get_real_mr_from_implicit_lkey(pd,
								  ilkey,
								  addr,
								  effective_length,
								  &mr);
		if (ret_val)
			return ret_val;
		attr.comp_mask = 0;
		attr.addr = (void *)(unsigned long)addr;
		attr.length = effective_length;
		attr.flags = flags;

		ret_val = ibv_exp_prefetch_mr(mr, &attr);
		if (ret_val)
			return ret_val;

		addr += effective_length;
	}
	return 0;
}
