#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "test_printf.h"
#include "util/huge_alloc.h"

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

void dereg_mr_wrapper(ERpc::MemRegInfo mr) { _unused(mr); }

using namespace std::placeholders;
typename ERpc::reg_mr_func_t reg_mr_func = std::bind(reg_mr_wrapper, _1, _2);
typename ERpc::dereg_mr_func_t dereg_mr_func = std::bind(dereg_mr_wrapper, _1);

/**
 * @brief Measure performance of page allocation where all pages are allocated
 * without first creating a page cache.
 *
 * E5-2450: 7.2 ns per page
 */
TEST(HugeAllocatorTest, PageAllocPerf) {
  /* Reserve all memory for high perf */
  ERpc::HugeAllocator *allocator = new ERpc::HugeAllocator(
      SYSTEM_HUGEPAGES * ERpc::kHugepageSize, 0, reg_mr_func, dereg_mr_func);

  size_t num_pages_allocated = 0;

  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);

  while (true) {
    void *buf = allocator->alloc(4 * 1024);
    if (buf == nullptr) {
      break;
    }

    num_pages_allocated++;
  }

  clock_gettime(CLOCK_REALTIME, &end);
  double nanoseconds =
      (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);

  test_printf("Time per page allocation = %.2f ns\n",
              nanoseconds / num_pages_allocated);

  delete allocator;
}

/**
 * @brief Measure performance of page allocation where pages are allocated from
 * the cache.
 */
TEST(HugeAllocatorTest, PageAllocPerfWithCache) {
  /* Reserve all memory for high perf */
  ERpc::HugeAllocator *allocator = new ERpc::HugeAllocator(
      SYSTEM_HUGEPAGES * ERpc::kHugepageSize, 0, reg_mr_func, dereg_mr_func);

  size_t page_cache_size = SYSTEM_4K_PAGES / 2;
  allocator->create_4k_chunk_cache(page_cache_size);

  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);

  for (size_t i = 0; i < page_cache_size; i++) {
    void *buf = allocator->alloc(4 * 1024);
    if (buf == nullptr) {
      break;
    }
  }

  clock_gettime(CLOCK_REALTIME, &end);
  double nanoseconds =
      (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);

  test_printf("Time per page allocation with page cache = %.2f ns\n",
              nanoseconds / page_cache_size);

  delete allocator;
}

/**
 * @brief Measure performance of page allocation where pages are allocated from
 * the cache, and the specialized alloc_4k function is used.
 */
TEST(HugeAllocatorTest, PageAllocPerfWithCacheWithSpecialAlloc) {
  /* Reserve all memory for high perf */
  ERpc::HugeAllocator *allocator = new ERpc::HugeAllocator(
      SYSTEM_HUGEPAGES * ERpc::kHugepageSize, 0, reg_mr_func, dereg_mr_func);

  size_t page_cache_size = SYSTEM_4K_PAGES / 2;
  allocator->create_4k_chunk_cache(page_cache_size);

  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);

  for (size_t i = 0; i < page_cache_size; i++) {
    void *buf = allocator->alloc_4k();
    if (buf == nullptr) {
      break;
    }
  }

  clock_gettime(CLOCK_REALTIME, &end);
  double nanoseconds =
      (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);

  test_printf("Time per page allocation with page cache = %.2f ns\n",
              nanoseconds / page_cache_size);

  delete allocator;
}

/**
 * @brief Allocate all hugepages as 2MB chunks once.
 */
TEST(HugeAllocatorTest, 2MBChunksSingleRun) {
  ERpc::HugeAllocator *allocator;

  allocator = new ERpc::HugeAllocator(1024, 0, reg_mr_func, dereg_mr_func);
  size_t num_hugepages_allocated = 0;
  for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
    void *buf = allocator->alloc(2 * 1024 * 1024);
    if (buf != nullptr) {
      num_hugepages_allocated++;
    } else {
      test_printf("Allocated %zu of %zu hugepages\n", num_hugepages_allocated,
                  SYSTEM_HUGEPAGES);
      break;
    }
  }

  allocator->print_stats();
  delete allocator;
}

/**
 * @brief Repeatedly allocate all huge pages as 2MB chunks.
 */
TEST(HugeAllocatorTest, 2MBChunksMultiRun) {
  ERpc::HugeAllocator *allocator;

  for (int iters = 0; iters < 20; iters++) {
    allocator = new ERpc::HugeAllocator(1024, 0, reg_mr_func, dereg_mr_func);
    for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
      void *buf = allocator->alloc(2 * 1024 * 1024);
      if (buf == nullptr) {
        break;
      }
    }

    delete allocator;
  }
}

/**
 * @brief Repeat: Try to allocate all memory as variable-length 2MB-aligned
 * chunks. When allocation finally fails, print out the memory efficiency.
 */
TEST(HugeAllocatorTest, VarMBChunksSingleRun) {
  struct free_info_t {
    void *buf;
    size_t size;

    free_info_t(void *buf, size_t size) : buf(buf), size(size) {}
  };

  ERpc::HugeAllocator *allocator;
  allocator = new ERpc::HugeAllocator(1024, 0, reg_mr_func, dereg_mr_func);

  for (size_t i = 0; i < 10; i++) {
    size_t app_memory = 0;

    /* Record the allocated buffers and their sizes so we can free them */
    std::vector<free_info_t> free_info_vec;

    while (true) {
      size_t num_hugepages = 1ul + (unsigned)(std::rand() % 15);
      size_t size = num_hugepages * ERpc::kHugepageSize;
      void *buf = allocator->alloc(size);

      if (buf == nullptr) {
        ASSERT_EQ(allocator->get_allocated_memory(), app_memory);

        test_printf(
            "Fraction of system memory reserved by allocator at "
            "failure = %.2f (best = 1.0)\n",
            (double)allocator->get_reserved_memory() /
                (SYSTEM_HUGEPAGES * ERpc::kHugepageSize));

        test_printf(
            "Fraction of memory reserved allocated to user = %.2f "
            "(best = 1.0)\n",
            ((double)allocator->get_allocated_memory() /
             allocator->get_reserved_memory()));
        break;
      } else {
        app_memory += (num_hugepages * ERpc::kHugepageSize);
        free_info_vec.push_back(free_info_t(buf, size));
      }
    }

    /* Free all allocated hugepages in random order */
    std::random_shuffle(free_info_vec.begin(), free_info_vec.end());
    for (free_info_t &free_info : free_info_vec) {
      allocator->free(free_info.buf, free_info.size);
    }

    ASSERT_EQ(allocator->get_allocated_memory(), 0);
  }

  delete allocator;
}

/**
 * @brief Try to allocate all memory as a mixture of variable-length 2MB-aligned
 * chunks and 4K pages.When allocation finally fails, print out the memory
 * efficiency.
 */
TEST(HugeAllocatorTest, MixedPageHugepageSingleRun) {
  ERpc::HugeAllocator *allocator;
  allocator = new ERpc::HugeAllocator(1024, 0, reg_mr_func, dereg_mr_func);

  size_t app_memory = 0;

  while (true) {
    void *buf = nullptr;

    bool alloc_hugepages = (std::rand() % 100) == 0;
    size_t new_app_memory;

    if (alloc_hugepages) {
      size_t num_hugepages = 1ul + (unsigned)(std::rand() % 15);
      buf = allocator->alloc(num_hugepages * ERpc::kHugepageSize);
      new_app_memory = (num_hugepages * ERpc::kHugepageSize);
    } else {
      buf = allocator->alloc(4 * KB(1));
      new_app_memory = ERpc::kPageSize;
    }

    if (buf == nullptr) {
      EXPECT_EQ(app_memory, allocator->get_allocated_memory());
      test_printf(
          "Fraction of system memory reserved by allocator at "
          "failure = %.2f\n",
          (double)allocator->get_reserved_memory() /
              (SYSTEM_HUGEPAGES * ERpc::kHugepageSize));

      test_printf("Fraction of memory reserved allocated to user = %.2f\n",
                  ((double)allocator->get_allocated_memory() /
                   allocator->get_reserved_memory()));
      break;
    } else {
      app_memory += new_app_memory;
    }
  }

  delete allocator;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
