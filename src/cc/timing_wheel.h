/**
 * @file timing_wheel.h
 * @brief Timing wheel implementation from Carousel [SIGCOMM 17]
 * Units: Microseconds or TSC for time, bytes/sec for throughput
 */

#ifndef ERPC_TIMING_WHEEL_H
#define ERPC_TIMING_WHEEL_H

#include <queue>
#include "cc/timely.h"
#include "common.h"
#include "sm_types.h"
#include "sslot.h"
#include "util/mempool.h"

namespace erpc {

static constexpr size_t kWheelBucketCap = 4;  /// Wheel entries per bucket

/// One entry in a timing wheel bucket
struct wheel_ent_t {
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
  size_t num_entries;
  wheel_bkt_t *next;
  wheel_bkt_t *last;
  wheel_ent_t entry[kWheelBucketCap];
};
static_assert(sizeof(wheel_bkt_t) == 120, "");

class TimingWheel {
 public:
  TimingWheel(size_t mtu, double freq_ghz, double wslot_granularity,
              HugeAlloc *huge_alloc)
      : mtu(mtu),
        freq_ghz(freq_ghz),
        wslot_granularity(wslot_granularity),
        horizon((kSessionCredits * mtu / kTimelyMinRate) * 100000.0),
        num_wslots(std::round(horizon / wslot_granularity) + 0.5),
        huge_alloc(huge_alloc),
        bkt_pool(huge_alloc) {
    rt_assert(num_wslots > 100, "Too few wheel slots");
    rt_assert(num_wslots < 500000, "Too many wheel slots");

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

    while (bkt != nullptr) {
      for (size_t i = 0; i < bkt->num_entries; i++) q.push(bkt->entry[i]);

      wheel_bkt_t *_tmp_next = bkt->next;

      reset_bkt(bkt);
      if (bkt != &wheel[ws_i]) bkt_pool.free(bkt);
      bkt = _tmp_next;
    }

    wheel[ws_i].last = &wheel[ws_i];  // Reset last pointer
  }

  void insert(size_t ws_i, wheel_ent_t ent) {
    wheel_bkt_t *last_bkt = wheel[ws_i].last;
    assert(last_bkt->next == nullptr);

    assert(last_bkt->num_entries < kWheelBucketCap);
    last_bkt->entry[last_bkt->num_entries] = ent;
    last_bkt->num_entries++;

    // If last_bkt is full, allocate a new one and make it the last
    if (last_bkt->num_entries == kWheelBucketCap) {
      wheel_bkt_t *new_bkt = alloc_bkt();
      last_bkt->next = new_bkt;
      wheel[ws_i].last = new_bkt;
    }
  }

 private:
  inline void reset_bkt(wheel_bkt_t *bkt) {
    bkt->next = nullptr;
    bkt->num_entries = 0;
  }

  wheel_bkt_t *alloc_bkt() {
    wheel_bkt_t *bkt = bkt_pool.alloc();  // Exception if allocation fails
    reset_bkt(bkt);
    return bkt;
  }

  const size_t mtu;
  const double freq_ghz;
  const double wslot_granularity;  ///< Time-granularity of a slot
  const double horizon;            ///< Time horizon of the wheel
  const size_t num_wslots;
  HugeAlloc *huge_alloc;

  wheel_bkt_t *wheel;
  size_t cur_wslot = 0;
  MemPool<wheel_bkt_t> bkt_pool;
};
}

#endif  // ERPC_TIMING_WHEEL_H
