#ifndef ERPC_BUFFER_H
#define ERPC_BUFFER_H

#include "common.h"

namespace ERpc {

class Buffer {
 public:
  Buffer(void *buf, size_t size, uint32_t lkey)
      : buf(buf), size(size), lkey(lkey) {}

  Buffer() : buf(nullptr) {}

  /// Since \p Buffer does not allocate its own \p buf, do nothing here.
  ~Buffer() {}

  inline bool is_valid() { return buf != nullptr; }

  static Buffer get_invalid_buffer() { return Buffer(nullptr, 0, 0); }

  void *buf;
  size_t size;
  uint32_t lkey;
};

}  // End ERpc

#endif  // ERPC_BUFFER_H
