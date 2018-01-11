#ifndef ERPC_TIMING_WHEEL_H
#define ERPC_TIMING_WHEEL_H

#include "common.h"
#include "sslot.h"
#include "util/mempool.h"

namespace erpc {

static constexpr size_t kWheelBucketCap = 5;  /// Wheel entries per bucket

/// One entry in a timing wheel bucket
struct wheel_ent_t {
  bool is_valid;  // True iff this entry is valid
  bool is_rfr;    // True iff this entry is an RFR
  SSlot *sslot;
  union {
    size_t pkt_num;
    pkthdr_t *pkthdr;
  };

  wheel_ent_t() : sslot(nullptr), pkthdr(nullptr) {}

  wheel_ent_t(SSlot *sslot, size_t pkt_num)
      : is_rfr(false), sslot(sslot), pkt_num(pkt_num) {}

  wheel_ent_t(SSlot *sslot, pkthdr_t *pkthdr)
      : is_rfr(false), sslot(sslot), pkthdr(pkthdr) {}
};
static_assert(sizeof(wheel_ent_t) == 24, "");

struct wheel_bkt_t {
  wheel_ent_t entry[kWheelBucketCap];
  wheel_bkt_t *next;
};
static_assert(sizeof(wheel_bkt_t) == 128, "");

class TimingWheel {
  TimingWheel(size_t num_wslots, HugeAlloc *huge_alloc)
      : num_wslots(num_wslots), huge_alloc(huge_alloc), bkt_pool(huge_alloc) {
    // wheel_buffer is leadked, and deleted later with the allocator
    Buffer wheel_buffer = huge_alloc->alloc_raw(
        num_wslots * sizeof(wheel_bkt_t), DoRegister::kFalse);
    rt_assert(wheel_buffer.buf != nullptr, "Failed to allocate wheel");

    wheel = reinterpret_cast<wheel_bkt_t *>(wheel_buffer.buf);
    for (size_t ws_i = 0; ws_i < num_wslots; ws_i++) reset_bkt(&wheel[ws_i]);
  }

  inline void reset_bkt(wheel_bkt_t *bkt) {
    bkt->next = nullptr;
    for (size_t ent_i = 0; ent_i < kWheelBucketCap; ent_i++) {
      bkt->entry[ent_i].is_valid = false;
    }
  }

  wheel_bkt_t *alloc_bkt() {
    wheel_bkt_t *bkt = bkt_pool.alloc();  // Exception if allocation fails
    reset_bkt(bkt);
    return bkt;
  }

  const size_t num_wslots;
  HugeAlloc *huge_alloc;

  MemPool<wheel_bkt_t> bkt_pool;
  wheel_bkt_t *wheel;
};
}

#endif  // ERPC_TIMING_WHEEL_H
