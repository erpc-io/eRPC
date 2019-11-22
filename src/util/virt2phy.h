// Credits: DPDK

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace erpc {

/**
 * @brief A class to translate any mapped virtual address in the current process
 * to its physical address.
 *
 * Requires root access.
 */
class Virt2Phy {
  static constexpr size_t kPfnMaskSize = 8;

 public:
  Virt2Phy() {
    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
      printf("%s(): cannot open /proc/self/pagemap\n", strerror(errno));
      exit(-1);
    }

    page_size = static_cast<size_t>(getpagesize());  // Standard page size
  }

  ~Virt2Phy() { close(fd); }

  /**
   * @brief Return the physical address of this virtual address
   * @return The physical address on success, zero on failure
   */
  uint64_t translate(const void *virtaddr) {
    auto virt_pfn = static_cast<unsigned long>(
        reinterpret_cast<uint64_t>(virtaddr) / page_size);
    size_t offset = sizeof(uint64_t) * virt_pfn;

    uint64_t page;
    int ret = pread(fd, &page, kPfnMaskSize, static_cast<long>(offset));

    if (ret < 0) {
      fprintf(stderr, "cannot read /proc/self/pagemap: %s\n", strerror(errno));
      return 0;
    } else if (ret != kPfnMaskSize) {
      fprintf(stderr,
              "read %d bytes from /proc/self/pagemap but expected %zu:\n", ret,
              kPfnMaskSize);
      return 0;
    }

    // The pfn (page frame number) are bits 0-54 (see pagemap.txt in linux
    // Documentation)
    if ((page & 0x7fffffffffffffULL) == 0) return 0;

    uint64_t physaddr = ((page & 0x7fffffffffffffULL) * page_size) +
                        (reinterpret_cast<uint64_t>(virtaddr) % page_size);

    return physaddr;
  }

 private:
  int fd;
  size_t page_size;
};

}  // namespace erpc
