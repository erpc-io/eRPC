#include "main.h"

extern void run_thread(struct thread_params *params);

#define MAIN_CALLER_ID 12345	/* Some caller ID unused by workers */

int main(int argc, char **argv)
{
	/* For thread safety testing, values must have space for two words */
	static_assert(VAL_SIZE % sizeof(uint64_t) == 0 &&
		VAL_SIZE >= 2 * sizeof(uint64_t), "Invalid VAL_SIZE");

	auto config = ::mica::util::Config::load_file("crcw.json");
	auto test_config = config.get("test");

	/* Parse config */
	int num_threads = (int) test_config.get("num_threads").get_int64();
	int num_keys = (int)
		test_config.get("num_keys_thousands").get_int64() * 1024;
	int batch_size = (int) test_config.get("batch_size").get_int64();
	int put_percentage = (int) test_config.get("put_percentage").get_int64();

	assert(num_threads > 0 && num_threads < MAX_THREADS);
	assert(num_keys > 0 && hrd_is_power_of_2(num_keys));
	assert(batch_size > 0 && num_keys % batch_size == 0);
	assert(put_percentage >= 0 && put_percentage <= 100);

	auto table_config = config.get("table");

	/* Initialize the thread param array to store pointer to MicaTable */
	auto param_arr = new thread_params[num_threads];
	MicaResult out_result;

	printf("main: CRCW mode. Initializing table for all threads "
		"(%d keys)\n", num_keys);

	FixedTableConfig::Alloc *alloc = new FixedTableConfig::Alloc(
		config.get("alloc"));
	MicaTable *table = new MicaTable(config.get("table"),
		VAL_SIZE, bkt_base_shm_key, alloc, true);	/* Primary */

	table->print_bucket_occupancy();

	for(int key_i = 0; key_i < num_keys; key_i++) {
		uint64_t key = key_i;

		/* Insert a value with identical first two words */
		test_val_t val;
		val.buf[0] = key;
		val.buf[1] = key;

		uint64_t key_hash = mica_hash(&key, sizeof(test_key_t));

		out_result = table->lock_bucket_hash(MAIN_CALLER_ID, key_hash);
		assert(out_result == MicaResult::kSuccess);	/* Single-threaded for now */
		
		out_result = table->set(MAIN_CALLER_ID, key_hash, key, (char *) &val);

		if(out_result != MicaResult::kSuccess) {
			printf("main: Setting key %d failed. Error = %s\n", key_i,
				::mica::table::ResultString(out_result).c_str());
			table->print_bucket_occupancy();
			exit(-1);
		}
	}

	table->print_bucket_occupancy();

	/* All thread share the table */
	for(int thr_i = 0; thr_i < num_threads; thr_i++) {
		param_arr[thr_i].table = table;
	}

	/* Launch the worker threads */
	auto thread_arr = new std::thread[num_threads];

	for(int thr_i = 0; thr_i < num_threads; thr_i++) {
		param_arr[thr_i].tid = thr_i;
		param_arr[thr_i].num_keys = num_keys;
		param_arr[thr_i].batch_size = batch_size;
		param_arr[thr_i].put_percentage = put_percentage;

		thread_arr[thr_i] = std::thread(run_thread, &param_arr[thr_i]);

		/* Pin thread thr_i to hardware thread 2 * thr_i */
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(2 * thr_i, &cpuset);
		int rc = pthread_setaffinity_np(thread_arr[thr_i].native_handle(),
			sizeof(cpu_set_t), &cpuset);
		if (rc != 0) {
			printf("Error calling pthread_setaffinity_np. Error %d\n", rc);
		}
	}

	for(int thr_i = 0; thr_i < num_threads; thr_i++) {
		printf("Master: waiting for thread %d\n", thr_i);
		thread_arr[thr_i].join();
		printf("Master: thread %d done\n", thr_i);
	}
	
	return EXIT_SUCCESS;
}
