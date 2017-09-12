#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <thread>
#include <chrono>

#include "mica/table/ltable.h"
#include "mica/table/fixedtable.h"
#include "mica/util/hash.h"

#define VAL_SIZE 40

#define MAX_THREADS 28
#define USE_PREFETCH 1	/* 0: None. 1: Table prefetch */
static_assert(USE_PREFETCH == 0 || USE_PREFETCH == 1, "");

/* SHM keys */
#define bkt_base_shm_key 1000
#define keyarr_base_shm_key 3000
#define valarr_base_shm_key 4000

using namespace std;
using namespace std::chrono;

typedef ::mica::table::BasicFixedTableConfig FixedTableConfig;
typedef ::mica::table::FixedTable<FixedTableConfig> MicaTable;

typedef ::mica::table::Result MicaResult;	/* An enum */

typedef uint64_t test_key_t;
struct test_val_t {
	uint64_t buf[VAL_SIZE / sizeof(uint64_t)] = {0};
};

struct thread_params {
	int tid;
	int num_keys;
	int batch_size;
	int put_percentage;
	MicaTable *table;
};

template <typename T>
static uint64_t mica_hash(const T* key, size_t key_length)
{
	return ::mica::util::hash(key, key_length);
}

/* Some functions from libhrd */
static inline uint32_t hrd_fastrand(uint64_t *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return (uint32_t) (*seed >> 32);
}

static inline int hrd_is_power_of_2(uint32_t n)
{
	return n && !(n & (n - 1));
}

#endif
