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
  MtQueue() : size_(0) {}
  std::queue<T> queue_;

  /// Add an element to the queue. Caller need not grab the lock.
  void unlocked_push(T t) {
    lock();
    queue_.push(t);
    memory_barrier();
    size_++;
    unlock();
  }

  /// Get the first element from the queue. Caller need not grab the lock.
  T unlocked_pop() {
    lock();
    T t = queue_.front();
    queue_.pop();
    memory_barrier();
    size_--;
    unlock();

    return t;
  }

 private:
  void lock() { return lock_.lock(); }
  void unlock() { return lock_.unlock(); }

 public:
  volatile size_t size_;

 private:
  std::mutex lock_;
};

}  // namespace erpc
