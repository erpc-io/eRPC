#ifndef ERPC_BUFFER_H
#define ERPC_BUFFER_H

#include "common.h"

namespace ERpc {

/// Variable-sized buffer
class Buffer {
 public:
  Buffer(void *buf, size_t size, uint32_t lkey)
      : buf(buf), size(size), lkey(lkey) {}

  /// Since \p Buffer does not allocate its own \p buf, do nothing here.
  ~Buffer();

  void *buf;
  size_t size;
  uint32_t lkey;
};

}  // End ERpc

#endif  // ERPC_BUFFER_H
