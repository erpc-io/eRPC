#pragma once

#include "common.h"

namespace erpc {

/// A class to hold a fixed-size buffer. The size of the buffer is read-only
/// after the Buffer is created.
class Buffer {
 public:
  Buffer(uint8_t *buf, size_t class_size, uint32_t lkey)
      : buf(buf), class_size(class_size), lkey(lkey) {}

  Buffer() {}

  /// Since \p Buffer does not allocate its own \p buf, do nothing here.
  ~Buffer() {}

  /// Return a string representation of this Buffer (excluding lkey)
  std::string to_string() const {
    std::ostringstream ret;
    ret << "[buf " << static_cast<void *>(buf) << ", "
        << "class sz " << class_size << "]";
    return ret.str();
  }

  /// The backing memory of this Buffer. The Buffer is invalid if this is null.
  uint8_t *buf;
  size_t class_size;  ///< The allocator's class size
  uint32_t lkey;      ///< The memory registration lkey
};

}  // namespace erpc
