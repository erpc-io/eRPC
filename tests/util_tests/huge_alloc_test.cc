#include "util/huge_alloc.h"
#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "util/test_printf.h"

#define SYSTEM_HUGEPAGES (512)  // The number of hugepages available
#define SYSTEM_4K_PAGES (SYSTEM_HUGEPAGES * 512)  // Number of 4K pages

#define DUMMY_MR_PTR (reinterpret_cast<void *>(0x3185))
#define DUMMY_LKEY (3186)

using namespace erpc;

// Dummy registration and deregistration functions
Transport::MemRegInfo reg_mr_wrapper(void *, size_t) {
  return Transport::MemRegInfo(DUMMY_MR_PTR,
                               DUMMY_LKEY);  // *transport_mr, lkey
}

void dereg_mr_wrapper(Transport::MemRegInfo mr) {
  _unused(mr);
  assert(mr.lkey == DUMMY_LKEY);
  assert(mr.transport_mr == DUMMY_MR_PTR);
}

using namespace std::placeholders;
typename Transport::reg_mr_func_t reg_mr_func =
    std::bind(reg_mr_wrapper, _1, _2);
typename Transport::dereg_mr_func_t dereg_mr_func =
    std::bind(dereg_mr_wrapper, _1);

/// Measure performance of 4k-page allocation
TEST(HugeAllocTest, PageAllocPerf) {
  // Reserve all memory for high perf
  erpc::HugeAlloc *alloc = new erpc::HugeAlloc(
      SYSTEM_HUGEPAGES * erpc::kHugepageSize, 0, reg_mr_func, dereg_mr_func);

  size_t num_pages_allocated = 0;
  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);

  while (true) {
    erpc::Buffer buffer = alloc->alloc(KB(4));
    if (buffer.buf == nullptr) break;

    num_pages_allocated++;
  }

  clock_gettime(CLOCK_REALTIME, &end);
  double nanoseconds =
      (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);

  test_printf(
      "Time per page allocation = %.2f ns. "
      "Fraction of pages allocated = %.2f (best = 1.0)\n",
      nanoseconds / num_pages_allocated,
      1.0 * num_pages_allocated / SYSTEM_4K_PAGES);

  delete alloc;
}

/// Allocate all hugepages as 2MB chunks once.
TEST(HugeAllocTest, 2MBChunksSingleRun) {
  erpc::HugeAlloc *alloc;

  alloc = new erpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);
  size_t num_hugepages_allocated = 0;

  for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
    erpc::Buffer buffer = alloc->alloc(MB(2));
    if (buffer.buf != nullptr) {
      EXPECT_EQ(buffer.lkey, DUMMY_LKEY);
      num_hugepages_allocated++;
    } else {
      test_printf("Allocated %zu of %zu hugepages\n", num_hugepages_allocated,
                  SYSTEM_HUGEPAGES);
      break;
    }
  }

  alloc->print_stats();
  delete alloc;
}

/// Repeatedly allocate all huge pages as 2MB chunks.
TEST(HugeAllocTest, 2MBChunksMultiRun) {
  erpc::HugeAlloc *alloc;

  for (int iters = 0; iters < 20; iters++) {
    alloc = new erpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);
    for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
      erpc::Buffer buffer = alloc->alloc(MB(2));
      if (buffer.buf == nullptr) break;

      EXPECT_EQ(buffer.lkey, DUMMY_LKEY);
    }

    delete alloc;
  }
}

/**
 * @brief Repeat: Try to allocate all memory as variable-length 2MB-aligned
 * chunks. When allocation finally fails, print out the memory efficiency.
 */
TEST(HugeAllocTest, VarMBChunksSingleRun) {
  erpc::HugeAlloc *alloc =
      new erpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);

  for (size_t i = 0; i < 10; i++) {
    size_t app_memory = 0;

    // Record the allocated buffers so we can free them
    std::vector<erpc::Buffer> buffer_vec;

    while (true) {
      size_t num_hugepages = 1ul + static_cast<unsigned>(std::rand() % 4);
      size_t size = num_hugepages * erpc::kHugepageSize;
      erpc::Buffer buffer = alloc->alloc(size);

      if (buffer.buf == nullptr) {
        test_printf(
            "Fraction of system memory reserved by alloc at "
            "failure = %.2f (best = 1.0)\n",
            1.0 * alloc->get_stat_shm_reserved() /
                (SYSTEM_HUGEPAGES * erpc::kHugepageSize));

        test_printf(
            "Fraction of memory reserved allocated to user = %.2f "
            "(best = 1.0)\n",
            1.0 * app_memory / alloc->get_stat_shm_reserved());

        break;
      } else {
        EXPECT_EQ(buffer.lkey, DUMMY_LKEY);
        app_memory += (num_hugepages * erpc::kHugepageSize);
        buffer_vec.push_back(buffer);
      }
    }

    // Free all allocated hugepages in random order
    std::random_shuffle(buffer_vec.begin(), buffer_vec.end());
    for (erpc::Buffer buffer : buffer_vec) alloc->free_buf(buffer);
  }

  delete alloc;
}

/**
 * @brief Try to allocate all memory as a mixture of variable-length 2MB-aligned
 * chunks and 4K pages.When allocation finally fails, print out the memory
 * efficiency.
 */
TEST(HugeAllocTest, MixedPageHugepageSingleRun) {
  erpc::HugeAlloc *alloc;
  alloc = new erpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);

  size_t app_memory = 0;

  while (true) {
    erpc::Buffer buffer;
    bool alloc_hugepages = (std::rand() % 100) == 0;
    size_t new_app_memory;

    if (alloc_hugepages) {
      size_t num_hugepages = 1ul + static_cast<unsigned>(std::rand() % 4);
      buffer = alloc->alloc(num_hugepages * erpc::kHugepageSize);
      new_app_memory = (num_hugepages * erpc::kHugepageSize);
    } else {
      buffer = alloc->alloc(KB(4));
      new_app_memory = KB(4);
    }

    if (buffer.buf == nullptr) {
      test_printf(
          "Fraction of system memory reserved by alloc at "
          "failure = %.2f\n",
          1.0 * alloc->get_stat_shm_reserved() /
              (SYSTEM_HUGEPAGES * erpc::kHugepageSize));

      test_printf("Fraction of memory reserved allocated to user = %.2f\n",
                  (1.0 * app_memory / alloc->get_stat_shm_reserved()));
      break;
    } else {
      EXPECT_EQ(buffer.lkey, DUMMY_LKEY);
      app_memory += new_app_memory;
    }
  }

  delete alloc;
}

/// Test raw allocation without registration and deregistration functions
TEST(HugeAllocTest, RawAlloc) {
  // kMaxClassSize gets reserved by the allocator on initialization, so thi
  // is the max size we can hope to get.
  size_t max_alloc_size =
      SYSTEM_HUGEPAGES * MB(2) - erpc::HugeAlloc::kMaxClassSize;

  // Try reserving max memory multiple times to test allocator destructor
  for (size_t i = 0; i < 5; i++) {
    auto *alloc = new erpc::HugeAlloc(1024, 0, nullptr, nullptr);
    Buffer buffer = alloc->alloc_raw(max_alloc_size, DoRegister::kFalse);
    ASSERT_NE(buffer.buf, nullptr);
    ASSERT_EQ(buffer.class_size, SIZE_MAX);
    delete alloc;
  }

  // Try some corner cases
  auto *alloc = new erpc::HugeAlloc(1024, 0, nullptr, nullptr);
  Buffer buffer = alloc->alloc_raw(1, DoRegister::kFalse);  // 1 byte
  ASSERT_NE(buffer.buf, nullptr);
  ASSERT_EQ(buffer.class_size, SIZE_MAX);

  buffer = alloc->alloc_raw(MB(16) * 1024 * 1024, DoRegister::kFalse);
  ASSERT_EQ(buffer.buf, nullptr);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
