#pragma once

#include <stdlib.h>
#include <mutex>
#include <queue>

#include "util/barrier.h"

namespace erpc {

/// A simple multi-threaded queue, without performance optimizations for the
/// the single-threaded case
template <class T>
class MtQueue {
 public:
  MtQueue() : size(0) {}
  std::queue<T> queue;

  /// Add an element to the queue. Caller need not grab the lock.
  void unlocked_push(T t) {
    lock();
    queue.push(t);
    memory_barrier();
    size++;
    unlock();
  }

  /// Get the first element from the queue. Caller need not grab the lock.
  T unlocked_pop() {
    lock();
    T t = queue.front();
    queue.pop();
    memory_barrier();
    size--;
    unlock();

    return t;
  }

 private:
  void lock() { return _lock.lock(); }
  void unlock() { return _lock.unlock(); }

 public:
  volatile size_t size;

 private:
  std::mutex _lock;
};

}  // namespace erpc
