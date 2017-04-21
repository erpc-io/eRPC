#ifndef MT_LIST_H
#define MT_LIST_H

#include "common.h"
#include "util/barrier.h"

namespace ERpc {

/// A simple multi-threaded list, without performance optimizations for the
/// the single-threaded case
template <class T>
class MtList {
 public:
  MtList() : size(0) {}
  volatile size_t size;
  std::vector<T> list;

  void lock() { return _lock.lock(); }
  void unlock() { return _lock.unlock(); }

  /// Clear this list. Caller must hold lock
  void locked_clear() {
    size = 0;
    list.clear();
  }

  /// Add an element to the list. This function grabs the list lock.
  void unlocked_push_back(T t) {
    lock();
    list.push_back(t);
    memory_barrier();
    size++;
    unlock();
  }

 private:
  std::mutex _lock;
};

}  // End ERpc

#endif // MT_LIST_H
