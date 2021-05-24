#pragma once

#include <atomic>
#include "common.h"

namespace erpc {

class TlsRegistry {
 public:
  TlsRegistry() : cur_etid_(0) {}
  std::atomic<size_t> cur_etid_;

  /// Initialize all the thread-local registry members
  void init();

  /// Reset all members
  void reset();

  /// Return the eRPC thread ID of the caller
  size_t get_etid() const;
};
}  // namespace erpc
