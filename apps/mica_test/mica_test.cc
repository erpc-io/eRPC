#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <chrono>
#include <unordered_set>

#include "mica/table/fixedtable.h"
#include "mica/util/hash.h"
#include "util/huge_alloc.h"

static constexpr bool kPrefetch = true;
static constexpr size_t kValSize = 16;

typedef mica::table::FixedTable<mica::table::BasicFixedTableConfig> FixedTable;
typedef FixedTable::ft_key_t test_key_t;

typedef mica::table::Result MicaResult;
struct test_val_t {
  uint64_t buf[kValSize / sizeof(uint64_t)];
};

template <typename T>
static uint64_t mica_hash(const T *key, size_t key_length) {
  return ::mica::util::hash(key, key_length);
}

static inline uint32_t hrd_fastrand(uint64_t *seed) {
  *seed = *seed * 1103515245 + 12345;
  return static_cast<uint32_t>(*seed >> 32);
}

int num_iters = 100;

int main() {
  uint64_t seed = 0xdeadbeef;
  std::chrono::high_resolution_clock timer;

  auto config = mica::util::Config::load_file("apps/mica_test/mica_test.json");

  size_t num_keys =
      static_cast<size_t>(config.get("test").get("num_keys").get_int64());
  assert(num_keys > 0);

  size_t batch_size =
      static_cast<size_t>(config.get("test").get("batch_size").get_int64());
  assert(batch_size > 0 && num_keys % batch_size == 0);

  // We'll only use alloc_raw, so no need for registration/deregistration funcs
  auto *alloc = new erpc::HugeAlloc(1024, 0, nullptr, nullptr);
  FixedTable table(config.get("table"), kValSize, alloc);

  erpc::Buffer key_buf =
      alloc->alloc_raw(num_keys * sizeof(test_key_t), erpc::DoRegister::kFalse);
  erpc::rt_assert(key_buf.buf != nullptr, "");
  auto *key_arr = reinterpret_cast<test_key_t *>(key_buf.buf);

  erpc::Buffer val_buf =
      alloc->alloc_raw(num_keys * sizeof(test_val_t), erpc::DoRegister::kFalse);
  erpc::rt_assert(val_buf.buf != nullptr, "");
  auto *val_arr = reinterpret_cast<test_val_t *>(val_buf.buf);

  uint64_t *key_hash_arr = new uint64_t[batch_size];
  MicaResult out_result;

  // This is more of a correctness test: the keys are queried in the same
  // order as they are inserted.
  for (int iter = 0; iter < num_iters; iter++) {
    // Populate key_arr
    printf("Iteration %d: Generating keys\n", iter);
    for (size_t i = 0; i < num_keys; i++) {
      test_key_t ft_key;
      for (size_t j = 0; j < sizeof(test_key_t) / 8; j++) {
        ft_key.qword[j] = hrd_fastrand(&seed);
      }

      key_arr[i] = ft_key;
      val_arr[i].buf[0] = key_arr[i].qword[0];
    }

    // Insert
    printf("Iteration %d: Setting keys\n", iter);
    auto start = timer.now();
    for (size_t i = 0; i < num_keys; i += batch_size) {
      for (size_t j = 0; j < batch_size; j++) {
        key_hash_arr[j] = mica_hash(&key_arr[i + j], sizeof(test_key_t));
        if (kPrefetch) table.prefetch_table(key_hash_arr[j]);
      }

      for (size_t j = 0; j < batch_size; j++) {
        out_result = table.set(key_hash_arr[j], key_arr[i + j],
                               reinterpret_cast<char *>(&val_arr[i + j]));
        if (out_result != MicaResult::kSuccess) {
          printf("Inserting key %zu failed. Error = %s\n", i + j,
                 ::mica::table::ResultString(out_result).c_str());
          exit(-1);
        }
      }
    }

    auto end = timer.now();
    double us =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start)
            .count();

    printf("Insert tput = %.2f M/s\n", num_keys / us);

    // GET
    start = timer.now();
    for (size_t i = 0; i < num_keys; i += batch_size) {
      for (size_t j = 0; j < batch_size; j++) {
        key_hash_arr[j] = mica_hash(&key_arr[i + j], sizeof(test_key_t));
        if (kPrefetch) table.prefetch_table(key_hash_arr[j]);
      }

      for (size_t j = 0; j < batch_size; j++) {
        test_val_t temp_val;

        out_result = table.get(key_hash_arr[j], key_arr[i + j],
                               reinterpret_cast<char *>(&temp_val));
        if (out_result != MicaResult::kSuccess) {
          printf("GET failed for key %zu. Error = %s\n", i + j,
                 ::mica::table::ResultString(out_result).c_str());
          exit(-1);
        }

        if (temp_val.buf[0] != key_arr[i + j].qword[0]) {
          printf("Bad value for key %lu. Expected = %lu, got = %lu\n",
                 key_arr[i + j].qword[0], key_arr[i + j].qword[0],
                 temp_val.buf[0]);
          exit(-1);
        }
      }
    }

    end = timer.now();
    us = std::chrono::duration_cast<std::chrono::microseconds>(end - start)
             .count();
    printf("Get tput = %.2f M/s\n", num_keys / us);

    // Delete
    for (size_t i = 0; i < num_keys; i++) {
      uint64_t key_hash = mica_hash(&key_arr[i], sizeof(test_key_t));
      // del() can return kNotFound because the input may have duplicates
      out_result = table.del(key_hash, key_arr[i]);
    }
  }

  printf("Done test\n");

  return EXIT_SUCCESS;
}
