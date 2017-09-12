#include "main.h"

void run_thread(struct thread_params *params)
{
	auto tid = params->tid;
	auto num_keys = params->num_keys;
	auto batch_size = params->batch_size;
	auto put_percentage = params->put_percentage;
	auto table = params->table;
	assert(table != NULL); 	/* main must have initialized the table */

	uint64_t seed = 0xdeadbeef;
	high_resolution_clock timer;

	/* Move fastrand */
	for(int i = 0; i < tid * 100000000; i++) {
		hrd_fastrand(&seed);
	}

	/* Arrays for batches of requests */
	test_key_t *key_arr = new test_key_t[batch_size];
	uint64_t *key_hash_arr = new uint64_t[batch_size];
	bool *is_put = new bool[batch_size];
	bool *lock_success = new bool[batch_size];

	/* Datastore result variables */
	test_val_t temp_val;
	MicaResult out_result;
	uint64_t timestamp;

	/* Measurement */
	size_t iter = 0;
	size_t num_locked = 0;
	
	auto start = timer.now();
	while(true) {
		iter++;

		for(int i = 0; i < batch_size; i++) {
			key_arr[i] = hrd_fastrand(&seed) & (num_keys - 1);
			is_put[i] = (hrd_fastrand(&seed) % 100 < (unsigned) put_percentage) ?
				true : false;
			key_hash_arr[i] = mica_hash(&key_arr[i], sizeof(test_key_t));
#if USE_PREFETCH == 1
			table->prefetch_table(key_hash_arr[i]);
#endif
		}

		/*
		 * Acquire all locks needed for this batch. This stresses the per-bucket
		 * number-of-locks maintenance in MICA.
		 */
		for(int i = 0; i < batch_size; i++) {
			if(is_put[i]) {
				out_result = table->lock_bucket_hash(tid, key_hash_arr[i]);
				if(out_result == MicaResult::kSuccess) {
					lock_success[i] = true;
				} else {
					assert(out_result == MicaResult::kLocked);
					num_locked++;	/* For stats */
					lock_success[i] = false;
					continue;
				}
			}
		}

		for(int i = 0; i < batch_size; i++) {
			if(!is_put[i]) {
				out_result = table->get(tid, key_hash_arr[i],
					key_arr[i], &timestamp, (char *) &temp_val);

				if(out_result != MicaResult::kSuccess) {
					assert(out_result == MicaResult::kLocked);
					num_locked++;
					continue;
				}
	
				/* get() was successful: check values */
				if(temp_val.buf[0] != temp_val.buf[1]) {
					printf("Worker %d: Unequal values for key %lu. = %lu, %lu\n",
						tid, key_arr[i], temp_val.buf[0], temp_val.buf[1]);
					exit(-1);
				}
			} else {
				/* set() should be called only if we got the bucket lock */
				if(lock_success[i] == false) {
					continue;
				}

				/*
				 * Insert a value with identical first two words. The words are
				 * either equal to key + tid, so that different threads write
				 * different values.
				 */
				temp_val.buf[0] = key_arr[i] + tid;
				temp_val.buf[1] = key_arr[i] + tid;

				/* We hold the bucket lock so it's safe to set() */
				out_result = table->set(tid, key_hash_arr[i],
					key_arr[i], (char *) &temp_val);

				if(out_result != MicaResult::kSuccess) {
					printf("Worker %d: Failed to set() key %lu. Error = %s\n",
						tid, key_arr[i],
						::mica::table::ResultString(out_result).c_str());
					exit(-1);
				}
			}
		}

		if(iter >= 1000000) {
			auto end = timer.now();
			double us = duration_cast<microseconds>(end - start).count();
			printf("Worker %d: Tput = %.2f M/s, locked fraction = %.5f "
				"put_percentage = %d\n",
				tid, iter * batch_size / us,
				(double) num_locked / iter, put_percentage);

			iter = 0;
			num_locked = 0;
			start = timer.now();
		}
	}
}
