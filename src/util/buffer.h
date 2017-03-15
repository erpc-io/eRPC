#ifndef ERPC_BUFFER_H
#define ERPC_BUFFER_H

#include "common.h"

namespace ERpc {

/// A class to hold a fixed-size buffer. The size of the buffer is read-only
/// after the Buffer is created.
class Buffer {
 public:
  Buffer(uint8_t *buf, size_t class_size, uint32_t lkey)
      : buf(buf), class_size(class_size), lkey(lkey) {}

  Buffer() {}

  /// Since \p Buffer does not allocate its own \p buf, do nothing here.
  ~Buffer() {}

  static Buffer get_invalid_buffer() { return Buffer(nullptr, 0, 0); }

  std::string to_string() const { return std::string(""); }

  /// The backing memory of this Buffer. The Buffer is invalid if this is NULL.
  uint8_t *buf = nullptr;
  size_t class_size = 0;  ///< The class size
  uint32_t lkey = 0;      ///< The memory registration lkey
};

}  // End ERpc

#endif  // ERPC_BUFFER_H
