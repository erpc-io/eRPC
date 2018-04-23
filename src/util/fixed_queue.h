#ifndef FIXED_QUEUE_H
#define FIXED_QUEUE_H

#include <assert.h>
#include <queue>
#include "common.h"

namespace erpc {

/**
 * @brief A *slow* static-sized queue that supports push and pop. When the queue
 * is full, old packets are removed in FIFO order.
 *
 * @tparam T The type of elements stored in the queue
 * @tparam N The size of the queue
 */
template <typename T, size_t N>
class FixedQueue {
 public:
  FixedQueue() {}
  ~FixedQueue() {}

  inline void push(T t) {
    if (queue.size() == N) queue.pop();
    queue.push(t);
  }

  inline T pop() {
    rt_assert(queue.size() != 0, "Cannot pop empty queue");
    T ret = queue.front();
    queue.pop();
    return ret;
  }

  /// Clear the queue
  inline void clear() {
    std::queue<T> empty;
    std::swap(queue, empty);
  }

  /// Return the number of elements currently in the queue
  inline size_t size() { return queue.size(); }

  /// Return the maximum capacity of the FixedQueue
  inline size_t capacity() { return N; }

 private:
  std::queue<T> queue;
};

}  // End erpc

#endif  // FIXED_QUEUE_H
