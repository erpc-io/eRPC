/**
 * @file pmem.h
 * @brief Utilities for persistent memory
 */

#pragma once

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string>
#include "common.h"
#include "math_utils.h"

namespace erpc {

uint8_t* map_devdax_file(std::string devdax_file, size_t bytes) {
  int fd = open(devdax_file.c_str(), O_RDWR);
  rt_assert(fd >= 0, "Failed to open devdax file " + devdax_file);

  size_t pmem_size = round_up<MB(2)>(bytes);  // Smaller sizes may fail
  void* buf =
      mmap(nullptr, pmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  rt_assert(buf != MAP_FAILED, "Failed to mmap devdax file" + devdax_file);
  rt_assert(reinterpret_cast<size_t>(buf) % 256 == 0);
  memset(buf, 0, pmem_size);

  return reinterpret_cast<uint8_t*>(buf);
}

void unmap_devdax(uint8_t* buf, size_t bytes) {
  munmap(reinterpret_cast<void*>(buf), bytes);
}
}  // namespace erpc
