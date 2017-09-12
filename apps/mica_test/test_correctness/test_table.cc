#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <chrono>
#include <unordered_set>

#include "mica/table/fixedtable.h"
#include "mica/util/hash.h"

/*
 * 0: No prefetch
 * 1: Prefetch table
 */
#define USE_PREFETCH 1
#define VAL_SIZE 16

using namespace std;
using namespace std::chrono;

struct FixedTableConfig : public ::mica::table::BasicFixedTableConfig {
  static constexpr bool kFetchAddOnlyIfEven = false;
};
typedef ::mica::table::FixedTable<FixedTableConfig> MicaTable;

typedef ::mica::table::Result MicaResult;	/* An enum */
typedef uint64_t test_key_t;
struct test_val_t {
	uint64_t buf[VAL_SIZE / sizeof(uint64_t)];
};

template <typename T>
static uint64_t mica_hash(const T* key, size_t key_length)
{
	return ::mica::util::hash(key, key_length);
}

static inline uint32_t hrd_fastrand(uint64_t *seed)
{
    *seed = *seed * 1103515245 + 12345;
    return (uint32_t) (*seed >> 32);
}

int num_iters = 100;

/* SHM keys */
int keyarr_shm_key = 1;
int valarr_shm_key = 2;
int bkt_shm_key = 3;
int pool_shm_key = 4;	/* Only valid for ltable */

int main()
{
	uint64_t seed = 0xdeadbeef;
	high_resolution_clock timer;

	auto config = ::mica::util::Config::load_file("test_table.json");
	int num_keys = (int) config.get("test").get("num_keys").get_int64();
	assert(num_keys > 0);
	int batch_size = (int) config.get("test").get("batch_size").get_int64();
	assert(batch_size > 0 && num_keys % batch_size == 0);

	FixedTableConfig::Alloc alloc(config.get("alloc"));
	MicaTable table(config.get("table"), VAL_SIZE, bkt_shm_key, &alloc, true);

	test_key_t *key_arr = (test_key_t *) alloc.hrd_malloc_socket(keyarr_shm_key,
		num_keys * sizeof(test_key_t), 0);
	test_val_t *val_arr = (test_val_t *) alloc.hrd_malloc_socket(valarr_shm_key,
		num_keys * sizeof(test_val_t), 0);
		
	uint64_t *key_hash_arr = new uint64_t[batch_size];
	uint64_t timestamp;
	MicaResult out_result;
	std::unordered_set<uint64_t> S;

	/*
	 * This is more of a correctness test: the keys are queried in the same
	 * order as they are inserted, causing sequential pool memory accesses.
	 */
	for(int iter = 0; iter < num_iters; iter++) {
		/* Populate @key_arr with unique keys */
		printf("Iteration %d: Generating keys\n", iter);
		for(int i = 0; i < num_keys; i++) {
			uint64_t key = hrd_fastrand(&seed);
			while(S.count(key) == 1) {
				key = hrd_fastrand(&seed);
			}

			S.insert(key);
			key_arr[i] = key;
			val_arr[i].buf[0] = key;
		}

		S.clear();

		/* Insert */
		printf("Iteration %d: Setting keys\n", iter);
		auto start = timer.now();
		for(int i = 0; i < num_keys; i += batch_size) {
			for(int j = 0; j < batch_size; j++) {
				key_hash_arr[j] = mica_hash(&key_arr[i + j], sizeof(test_key_t));
#if USE_PREFETCH == 1
				table.prefetch_table(key_hash_arr[j]);
#endif
			}

			for(int j = 0; j < batch_size; j++) {
				/* The key does not exist, so lock_bkt_for_ins should succeed */
				out_result = table.lock_bkt_for_ins(0, key_hash_arr[j],
					key_arr[i + j], &timestamp);
				if(out_result != MicaResult::kSuccess) {
					printf("Lock for bucket for key %d failed. Error = %s\n",
						i + j, ::mica::table::ResultString(out_result).c_str());
					exit(-1);
				}

				out_result = table.set(0, key_hash_arr[j],
					key_arr[i + j], (char *) &val_arr[i + j]);
				if(out_result != MicaResult::kSuccess) {
					printf("Inserting key %d failed. Error = %s\n", i + j,
						::mica::table::ResultString(out_result).c_str());
					exit(-1);
				}
			}
		}

		auto end = timer.now();
		double us = duration_cast<microseconds>(end - start).count();
		printf("Insert tput = %.2f M/s\n", num_keys / us);

		/* GET */
		start = timer.now();
		for(int i = 0; i < num_keys; i += batch_size) {
			for(int j = 0; j < batch_size; j++) {
				key_hash_arr[j] = mica_hash(&key_arr[i + j], sizeof(test_key_t));
#if USE_PREFETCH == 1
				table.prefetch_table(key_hash_arr[j]);
#endif
			}

			for(int j = 0; j < batch_size; j++) {
				test_val_t temp_val;

				out_result = table.get(0, key_hash_arr[j],
					key_arr[i + j], &timestamp, (char *) &temp_val);
				if(out_result != MicaResult::kSuccess) {
					printf("GET failed for key %d. Error = %s\n", i + j,
						::mica::table::ResultString(out_result).c_str());
					exit(-1);
				}

				if(temp_val.buf[0] != key_arr[i + j]) {
					printf("Bad value for key %lu. Expected = %lu, got = %lu\n",
						key_arr[i + j], key_arr[i + j], temp_val.buf[0]);
					exit(-1);
				}
			}
		}

		end = timer.now();
		us = duration_cast<microseconds>(end - start).count();
		printf("Get tput = %.2f M/s\n", num_keys / us);

		/* Delete */
		for(int i = 0; i < num_keys; i++) {
			uint64_t key_hash = mica_hash(&key_arr[i], sizeof(test_key_t));

			out_result = table.lock_bucket_hash(0, key_hash);
			assert(out_result == MicaResult::kSuccess);

			out_result = table.del(0, key_hash, key_arr[i]);
			if(out_result != MicaResult::kSuccess) {
				printf("Delete failed for key %d\n", i);
				exit(-1);
			}
		}
	}
	
	printf("Done test\n");
	(void) out_result;

	return EXIT_SUCCESS;
}
