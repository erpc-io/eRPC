#pragma once

#include <assert.h>
#include <libpmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include "../apps_common.h"

class Counter {
 public:
  static constexpr size_t kNumBuffers = 16;
  static constexpr size_t kBufferSize = 256;

  /**
   * @brief Construct a counter
   *
   * @param pbuf The start address of the counter on persistent memory
   *
   * @param create_new If true, the counter is reset to zero. If false, the
   * counter is initialized using the prior pmem contents.
   */
  Counter(uint8_t *pbuf, bool create_new) : ctr_base_addr(pbuf) {
    if (create_new) {
      pmem_memset_persist(pbuf, 0, kNumBuffers * kBufferSize);
    } else {
      size_t cur_max = 0;    // Maximum value among the counters
      size_t cur_max_i = 0;  // Index of the maximum value
      for (size_t i = 0; i < kNumBuffers; i++) {
        size_t *counter_i = reinterpret_cast<size_t *>(&pbuf[i * kBufferSize]);
        if (*counter_i > cur_max) {
          cur_max = *counter_i;
          cur_max_i = i;
        }
      }

      v_value = cur_max;
      buffer_idx = (cur_max_i + 1) % kNumBuffers;
    }
  }

  Counter() {}

  /// The amount of contiguous pmem needed for this counter
  static size_t get_reqd_space() {
    return erpc::round_up<256>(kNumBuffers * kBufferSize);
  }

  // Increment by always writing to the same location
  inline void increment_naive(size_t increment) {
    v_value += increment;
    pmem_memcpy_persist(&ctr_base_addr[0], &v_value, sizeof(v_value));
  }

  // Increment by writing to rotating locations, but don't do full-cacheline
  // writes
  inline void increment_rotate(size_t increment) {
    v_value += increment;
    pmem_memcpy_persist(&ctr_base_addr[buffer_idx * kBufferSize], &v_value,
                        sizeof(v_value));
    buffer_idx = (buffer_idx + 1) % kNumBuffers;
  }

  size_t v_value = 0;  // Volatile value of the counter

  size_t buffer_idx = 0;
  uint8_t *ctr_base_addr = nullptr;  // Starting address of the counter on pmem
};

class Log {
 public:
  // Assume pmem_file_size is large enough for any one write
  Log(uint8_t *pbuf, size_t pmem_file_size)
      : pmem_file_size(pmem_file_size), pbuf(pbuf) {
    ctr = Counter(pbuf, true /* create_new */);
    cur_write_offset = Counter::get_reqd_space();
  }

  Log() {}

  // Append with naive counter incrementing
  void append_naive(uint8_t *data, size_t data_size) {
    pmem_memcpy_persist(&pbuf[cur_write_offset], data, data_size);
    ctr.increment_naive(data_size);

    cur_write_offset += data_size;
    if (cur_write_offset + data_size > pmem_file_size) {
      cur_write_offset = Counter::get_reqd_space();
    }
  }

  // Append with rotating counter incrementing
  void append_rotating(uint8_t *data, size_t data_size) {
    pmem_memcpy_persist(&pbuf[cur_write_offset], data, data_size);
    ctr.increment_rotate(data_size);

    cur_write_offset += data_size;
    if (cur_write_offset + data_size > pmem_file_size) {
      cur_write_offset = Counter::get_reqd_space();
    }
  }

  size_t pmem_file_size;
  uint8_t *pbuf;
  size_t cur_write_offset = 0;
  Counter ctr;
};

void local_log_bench(uint8_t *pbuf, size_t pmem_file_size) {
  // Amount of data appended to the log in on iteration
  static constexpr size_t kMaxLogDataSize = 4096;
  static constexpr size_t kNumIters = 1000000;

  uint8_t source[kMaxLogDataSize] = {0};

  printf("Local log benchmark...\n");
  printf("write_bytes naive_GBps rotating_GBps\n");

  // Sweep over write sizes
  for (size_t write_sz = 64; write_sz <= kMaxLogDataSize; write_sz *= 2) {
    double naive_GBps, rotating_GBps;

    {
      // Naive log
      Log log(pbuf, pmem_file_size);
      struct timespec bench_start;
      clock_gettime(CLOCK_REALTIME, &bench_start);

      for (size_t i = 0; i < kNumIters; i++) {
        // Modify the source
        for (size_t j = 0; j < write_sz / 64; j += 64) source[j]++;
        log.append_naive(source, write_sz);
      }

      double bench_seconds = erpc::sec_since(bench_start);
      naive_GBps = kNumIters * write_sz / (bench_seconds * GB(1));
    }

    {
      // Rotating log
      Log log(pbuf, pmem_file_size);
      struct timespec bench_start;
      clock_gettime(CLOCK_REALTIME, &bench_start);

      for (size_t i = 0; i < kNumIters; i++) {
        // Modify the source
        for (size_t j = 0; j < write_sz / 64; j += 64) source[j]++;
        log.append_rotating(source, write_sz);
      }

      double bench_seconds = erpc::sec_since(bench_start);
      rotating_GBps = kNumIters * write_sz / (bench_seconds * GB(1));
    }

    printf("%zu %.2f %.2f\n", write_sz, naive_GBps, rotating_GBps);
  }

  printf("...done!\n");
}
