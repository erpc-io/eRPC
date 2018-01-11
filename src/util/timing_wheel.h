#ifndef ERPC_TIMING_WHEEL_H
#define ERPC_TIMING_WHEEL_H

#include <queue>
#include "common.h"
#include "sslot.h"
#include "util/mempool.h"

namespace erpc {

static constexpr size_t kWheelBucketCap = 5;  /// Wheel entries per bucket

/// One entry in a timing wheel bucket
struct wheel_ent_t {
  /// Number of entries in this entry's bucket. This field is valid only for
  /// the first entry in each bucket.
  uint8_t num_entries;

  bool is_rfr;  ///< True iff this entry is an RFR
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
  wheel_bkt_t *last;
};
static_assert(sizeof(wheel_bkt_t) % 8 == 0, "");

class TimingWheel {
 public:
  TimingWheel(size_t num_wslots, HugeAlloc *huge_alloc)
      : num_wslots(num_wslots), huge_alloc(huge_alloc), bkt_pool(huge_alloc) {
    // wheel_buffer is leaked, and deleted later with the allocator
    Buffer wheel_buffer = huge_alloc->alloc_raw(
        num_wslots * sizeof(wheel_bkt_t), DoRegister::kFalse);
    rt_assert(wheel_buffer.buf != nullptr, "Failed to allocate wheel");

    wheel = reinterpret_cast<wheel_bkt_t *>(wheel_buffer.buf);
    for (size_t ws_i = 0; ws_i < num_wslots; ws_i++) {
      reset_bkt(&wheel[ws_i]);
      wheel[ws_i].last = &wheel[ws_i];
    }
  }

  /// Transfer all entries from a wheel slot to a queue. The wheel slot is
  /// reset in the process
  void reap(std::queue<wheel_ent_t> &q, size_t ws_i) {
    wheel_bkt_t *bkt = &wheel[ws_i];
    while (true) {
      size_t n_entries = bkt->entry[0].num_entries;
      for (size_t i = 0; i < n_entries; i++) q.push(bkt->entry[i]);
      if (bkt->next == nullptr) break;

      wheel_bkt_t *_tmp_next = bkt->next;
      reset_bkt(bkt);
      bkt = _tmp_next;
    }

    wheel[ws_i].last = &wheel[ws_i];
  }

  void insert(size_t ws_i, wheel_ent_t ent) {
    wheel_bkt_t *last_bkt = wheel[ws_i].last;
    assert(last_bkt->next == nullptr);

    uint8_t &n_entries = last_bkt->entry[0].num_entries;
    assert(n_entries < kWheelBucketCap);
    last_bkt->entry[n_entries] = ent;
    n_entries++;

    // If last_bkt is full, allocate a new one and make it the last
    if (n_entries == kWheelBucketCap) {
      wheel_bkt_t *bkt = alloc_bkt();
      last_bkt->next = bkt;
      wheel[ws_i].last = bkt;
    }
  }

 private:
  inline void reset_bkt(wheel_bkt_t *bkt) {
    bkt->next = nullptr;
    bkt->entry[0].num_entries = 0;
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
