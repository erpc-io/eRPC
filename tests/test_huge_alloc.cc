#include "huge_alloc.h"
#include "test_printf.h"
#include <gtest/gtest.h>

#define SYSTEM_HUGEPAGES (512) /* The number of hugepages availabe */

/**
 * @brief Allocate all hugepages as 2MB chunks once.
 */
TEST(HugeAllocatorTest, 2MBChunksSingleRun) {
  ERpc::HugeAllocator *allocator;

  allocator = new ERpc::HugeAllocator(1024, 0);
  for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
    allocator->alloc_huge(2 * 1024 * 1024);
  }
  delete allocator;
}

/**
 * @brief Repeatedly allocate all huge pages as 2MB chunks.
 */
TEST(HugeAllocatorTest, 2MBChunksMultiRun) {
  ERpc::HugeAllocator *allocator;

  for (int iters = 0; iters < 20; iters++) {
    allocator = new ERpc::HugeAllocator(1024, 0);
    for (int i = 0; i < SYSTEM_HUGEPAGES; i++) {
      allocator->alloc_huge(2 * 1024 * 1024);
    }

    delete allocator;
  }
}

/**
 * @brief Try to allocate most hugepages as variable-length 2MB-aligned chunks.
 * When allocation finally fails, print out the memory efficiency.
 */
TEST(HugeAllocatorTest, VarMBChunksSingleRun) {
  ERpc::HugeAllocator *allocator;
  allocator = new ERpc::HugeAllocator(1024, 0);

  size_t app_memory = 0;

  while (true) {
    size_t num_hugepages = 1ul + (unsigned)(std::rand() % 15);
    void *buf = allocator->alloc_huge(num_hugepages * ERpc::kHugepageSize);

    if (buf == NULL) {
      test_printf("Fraction of system memory consumed by allocator before "
                  "failure = %.2f\n",
                  (double)allocator->get_total_memory() /
                      (SYSTEM_HUGEPAGES * ERpc::kHugepageSize));

      test_printf("Fraction of memory used by allocator actually given to "
                  "application = %.2f\n",
                  ((double)app_memory / allocator->get_total_memory()));
      break;
    } else {
      app_memory += (num_hugepages * ERpc::kHugepageSize);
    }
  }

  delete allocator;
}

/**
 * @brief Try to allocate most hugepages as variable-length 2MB-aligned chunks.
 * When allocation finally fails, print out the memory efficiency.
 */
TEST(HugeAllocatorTest, MixedPageHugepageSingleRun) {
  ERpc::HugeAllocator *allocator;
  allocator = new ERpc::HugeAllocator(1024, 0);

  size_t app_memory = 0;

  while (true) {
    void *buf = NULL;

    bool alloc_hugepages = (std::rand() % 100) == 0;
    size_t new_app_memory;

    if (alloc_hugepages) {
      size_t num_hugepages = 1ul + (unsigned)(std::rand() % 15);
      buf = allocator->alloc_huge(num_hugepages * ERpc::kHugepageSize);
      new_app_memory = (num_hugepages * ERpc::kHugepageSize);
    } else {
      buf = allocator->alloc_page();
      new_app_memory = ERpc::kPageSize;
    }

    if (buf == NULL) {
      test_printf("Fraction of system memory consumed by allocator before "
                  "failure = %.2f\n",
                  (double)allocator->get_total_memory() /
                      (SYSTEM_HUGEPAGES * ERpc::kHugepageSize));

      test_printf("Fraction of memory used by allocator actually given to "
                  "application = %.2f\n",
                  ((double)app_memory / allocator->get_total_memory()));
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
