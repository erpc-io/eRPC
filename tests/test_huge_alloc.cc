#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "util/huge_alloc.h"
#include "util/test_printf.h"

#define SYSTEM_HUGEPAGES (512) /* The number of hugepages available */
#define SYSTEM_4K_PAGES (SYSTEM_HUGEPAGES * 512) /* Number of 4K pages*/

#define DUMMY_MR_PTR ((void *)0x3185)
#define DUMMY_LKEY (3186)

// Dummy registration and deregistration functions
ERpc::MemRegInfo reg_mr_wrapper(void *buf, size_t size) {
  _unused(buf);
  _unused(size);
  return ERpc::MemRegInfo(DUMMY_MR_PTR, DUMMY_LKEY); /* *transport_mr, lkey */
}

void dereg_mr_wrapper(ERpc::MemRegInfo mr) {
  _unused(mr);
  assert(mr.lkey == DUMMY_LKEY);
  assert(mr.transport_mr == DUMMY_MR_PTR);
}

using namespace std::placeholders;
typename ERpc::reg_mr_func_t reg_mr_func = std::bind(reg_mr_wrapper, _1, _2);
typename ERpc::dereg_mr_func_t dereg_mr_func = std::bind(dereg_mr_wrapper, _1);

/// Measure performance of 4k-page allocation where all pages are allocated
/// without first creating a page cache.
TEST(HugeAllocTest, PageAllocPerf) {
  /* Reserve all memory for high perf */
  ERpc::HugeAlloc *alloc = new ERpc::HugeAlloc(
      SYSTEM_HUGEPAGES * ERpc::kHugepageSize, 0, reg_mr_func, dereg_mr_func);

  size_t num_pages_allocated = 0;
  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);

  while (true) {
    ERpc::Buffer buffer = alloc->alloc(KB(4));
    if (buffer.buf == nullptr) {
      break;
    }

    num_pages_allocated++;
  }

  clock_gettime(CLOCK_REALTIME, &end);
  double nanoseconds =
      (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);

  test_printf(
      "Time per page allocation = %.2f ns. "
      "Fraction of pages allocated = %.2f (best = 1.0)\n",
      nanoseconds / num_pages_allocated,
      (double)num_pages_allocated / SYSTEM_4K_PAGES);

  delete alloc;
}

/// Measure performance of page allocation with a cache
TEST(HugeAllocTest, PageAllocPerfWithCache) {
  /* Reserve all memory for high perf */
  ERpc::HugeAlloc *alloc = new ERpc::HugeAlloc(
      SYSTEM_HUGEPAGES * ERpc::kHugepageSize, 0, reg_mr_func, dereg_mr_func);

  size_t page_cache_size = SYSTEM_4K_PAGES / 2;
  alloc->create_cache(KB(4), page_cache_size);

  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);

  size_t num_pages_allocated = 0;
  for (size_t i = 0; i < page_cache_size; i++) {
    ERpc::Buffer buffer = alloc->alloc(KB(4));
    if (buffer.buf == nullptr) {
      break;
    }

    num_pages_allocated++;
  }

  clock_gettime(CLOCK_REALTIME, &end);
  double nanoseconds =
      (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);

  test_printf(
      "Time per page allocation with page cache = %.2f ns. "
      "Fraction of pages allocated = %.2f (best = 1.0)\n",
      nanoseconds / page_cache_size,
      (double)num_pages_allocated / page_cache_size);

  delete alloc;
}

/// Allocate all hugepages as 2MB chunks once.
TEST(HugeAllocTest, 2MBChunksSingleRun) {
  ERpc::HugeAlloc *alloc;

  alloc = new ERpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);
  size_t num_hugepages_allocated = 0;

  for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
    ERpc::Buffer buffer = alloc->alloc(MB(2));
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
  ERpc::HugeAlloc *alloc;

  for (int iters = 0; iters < 20; iters++) {
    alloc = new ERpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);
    for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
      ERpc::Buffer buffer = alloc->alloc(MB(2));
      if (buffer.buf == nullptr) {
        break;
      }

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
  ERpc::HugeAlloc *alloc =
      new ERpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);

  for (size_t i = 0; i < 10; i++) {
    size_t app_memory = 0;

    /* Record the allocated buffers so we can free them */
    std::vector<ERpc::Buffer> buffer_vec;

    while (true) {
      size_t num_hugepages = 1ul + (unsigned)(std::rand() % 4);
      size_t size = num_hugepages * ERpc::kHugepageSize;
      ERpc::Buffer buffer = alloc->alloc(size);

      if (buffer.buf == nullptr) {
        test_printf(
            "Fraction of system memory reserved by alloc at "
            "failure = %.2f (best = 1.0)\n",
            (double)alloc->get_stat_shm_reserved() /
                (SYSTEM_HUGEPAGES * ERpc::kHugepageSize));

        test_printf(
            "Fraction of memory reserved allocated to user = %.2f "
            "(best = 1.0)\n",
            (double)app_memory / alloc->get_stat_shm_reserved());

        break;
      } else {
        EXPECT_EQ(buffer.lkey, DUMMY_LKEY);
        app_memory += (num_hugepages * ERpc::kHugepageSize);
        buffer_vec.push_back(buffer);
      }
    }

    /* Free all allocated hugepages in random order */
    std::random_shuffle(buffer_vec.begin(), buffer_vec.end());
    for (ERpc::Buffer buffer : buffer_vec) {
      alloc->free_buf(buffer);
    }
  }

  delete alloc;
}

/**
 * @brief Try to allocate all memory as a mixture of variable-length 2MB-aligned
 * chunks and 4K pages.When allocation finally fails, print out the memory
 * efficiency.
 */
TEST(HugeAllocTest, MixedPageHugepageSingleRun) {
  ERpc::HugeAlloc *alloc;
  alloc = new ERpc::HugeAlloc(1024, 0, reg_mr_func, dereg_mr_func);

  size_t app_memory = 0;

  while (true) {
    ERpc::Buffer buffer;
    bool alloc_hugepages = (std::rand() % 100) == 0;
    size_t new_app_memory;

    if (alloc_hugepages) {
      size_t num_hugepages = 1ul + (unsigned)(std::rand() % 4);
      buffer = alloc->alloc(num_hugepages * ERpc::kHugepageSize);
      new_app_memory = (num_hugepages * ERpc::kHugepageSize);
    } else {
      buffer = alloc->alloc(KB(4));
      new_app_memory = ERpc::kPageSize;
    }

    if (buffer.buf == nullptr) {
      test_printf(
          "Fraction of system memory reserved by alloc at "
          "failure = %.2f\n",
          (double)alloc->get_stat_shm_reserved() /
              (SYSTEM_HUGEPAGES * ERpc::kHugepageSize));

      test_printf("Fraction of memory reserved allocated to user = %.2f\n",
                  ((double)app_memory / alloc->get_stat_shm_reserved()));
      break;
    } else {
      EXPECT_EQ(buffer.lkey, DUMMY_LKEY);
      app_memory += new_app_memory;
    }
  }

  delete alloc;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
