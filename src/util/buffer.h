#ifndef ERPC_BUFFER_H
#define ERPC_BUFFER_H

#include "common.h"

namespace ERpc {

class Buffer {
 public:
  Buffer(uint8_t *buf, uint32_t size, uint32_t lkey)
      : buf(buf), size(size), lkey(lkey) {}

  Buffer() : buf(nullptr), size(0), lkey(0) {}

  /// Since \p Buffer does not allocate its own \p buf, do nothing here.
  ~Buffer() {}

  inline bool is_valid() { return buf != nullptr; }

  static Buffer get_invalid_buffer() { return Buffer(nullptr, 0, 0); }
  size_t get_size() { return (size_t)size; }
  uint32_t get_lkey() { return lkey; }

  uint8_t *buf;

 private:
  uint32_t size; /* Make Buffer fit in 16 bytes */
  uint32_t lkey;
};

/* We need to keep sizeof(Buffer) small so we can use it in call-by-value */
static_assert(sizeof(Buffer) == 16, "");

}  // End ERpc

#endif  // ERPC_BUFFER_H
