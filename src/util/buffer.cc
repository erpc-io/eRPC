#include "buffer.h"

namespace ERpc {
Buffer::Buffer(size_t size) : size(size) { buf = (void *)malloc(size); }

Buffer::~Buffer() {
  assert(buf != NULL);
  free(buf);
}

}
