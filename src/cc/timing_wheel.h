/**
 * @file timing_wheel.h
 * @brief Timing wheel implementation from Carousel [SIGCOMM 17]
 * Units: Microseconds or TSC for time, bytes/sec for throughput
 *
 * For clarity and testing, the timing wheel must not call rdtsc() directly.
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
  size_t num_entries : 8;
  size_t tx_tsc : 56;  // Timestamp at which it is safe to transmit
  wheel_bkt_t *last;
  wheel_bkt_t *next;
  wheel_ent_t entry[kWheelBucketCap];
};
static_assert(sizeof(wheel_bkt_t) == 120, "");

struct timing_wheel_args_t {
  size_t mtu;
  double freq_ghz;
  size_t base_tsc;
  double wslot_width;
  HugeAlloc *huge_alloc;
};

class TimingWheel {
 public:
  TimingWheel(timing_wheel_args_t args)
      : mtu(args.mtu),
        freq_ghz(args.freq_ghz),
        wslot_width(args.wslot_width),
        wslot_width_tsc(us_to_cycles(wslot_width, freq_ghz)),
        horizon(1000000 * (kSessionCredits * mtu) / kTimelyMinRate),
        horizon_tsc(us_to_cycles(horizon, freq_ghz)),
        num_wslots(round_up(horizon / wslot_width)),
        huge_alloc(args.huge_alloc),
        bkt_pool(huge_alloc),
        debug_max_user_tsc(args.base_tsc) {
    rt_assert(wslot_width > .1 && wslot_width < 8.0, "Invalid wslot width");
    rt_assert(num_wslots > 100, "Too few wheel slots");
    rt_assert(num_wslots < 500000, "Too many wheel slots");

    // wheel_buffer is leaked, and deleted later with the allocator
    Buffer wheel_buffer = huge_alloc->alloc_raw(
        num_wslots * sizeof(wheel_bkt_t), DoRegister::kFalse);
    rt_assert(wheel_buffer.buf != nullptr, "Failed to allocate wheel");

    wheel = reinterpret_cast<wheel_bkt_t *>(wheel_buffer.buf);
    for (size_t ws_i = 0; ws_i < num_wslots; ws_i++) {
      reset_bkt(&wheel[ws_i]);
      wheel[ws_i].tx_tsc = args.base_tsc + (ws_i + 1) * wslot_width_tsc;
      wheel[ws_i].last = &wheel[ws_i];
    }

    cur_wslot = 0;
  }

  /// Move entries from all wheel slots older than reap_tsc to the ready queue.
  /// The max_tsc of all these wheel slots is advanced.
  /// This function must be called with non-decreasing values of reap_tsc
  void reap(size_t reap_tsc) {
    assert(reap_tsc >= debug_max_user_tsc);
    debug_max_user_tsc = reap_tsc;

    while (wheel[cur_wslot].tx_tsc <= reap_tsc) {
      /*
      printf("cur_wslot = %zu, slot tx_tsc = %zu, reap_tsc = %zu\n", cur_wslot,
             wheel[cur_wslot].tx_tsc, reap_tsc);
      */

      reap_wslot(cur_wslot);
      wheel[cur_wslot].tx_tsc += wslot_width_tsc;

      cur_wslot++;
      if (cur_wslot == num_wslots) cur_wslot = 0;
    }
  }

  /// Queue an entry for transmission at timestamp = abs_tx_tsc
  /// compute_tsc is a timestamp in the past at which abs_tx_tsc was computed by
  /// the rate controller.
  ///
  /// This function must be called with non-decreasing values of compute_tsc.
  void insert(const wheel_ent_t &ent, size_t compute_tsc, size_t abs_tx_tsc) {
    assert(compute_tsc >= debug_max_user_tsc);
    debug_max_user_tsc = compute_tsc;

    assert(abs_tx_tsc - compute_tsc <= horizon_tsc);

    // Advance the wheel to the compute_tsc
    reap(compute_tsc);
    assert(wheel[cur_wslot].tx_tsc > compute_tsc);

    size_t wslot_delta = (abs_tx_tsc - compute_tsc) / wslot_width_tsc;
    assert(wslot_delta < num_wslots);

    size_t dst_wslot = (cur_wslot + wslot_delta);
    if (dst_wslot >= num_wslots) dst_wslot -= num_wslots;

    insert_into_wslot(dst_wslot, ent);
  }

 private:
  void insert_into_wslot(size_t ws_i, const wheel_ent_t &ent) {
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

  /// Transfer all entries from a wheel slot to the ready queue. The wheel slot
  /// is reset and its chained buckets returned to the pool.
  void reap_wslot(size_t ws_i) {
    wheel_bkt_t *bkt = &wheel[ws_i];
    while (bkt != nullptr) {
      for (size_t i = 0; i < bkt->num_entries; i++) {
        ready_queue.push(bkt->entry[i]);
      }

      wheel_bkt_t *_tmp_next = bkt->next;

      reset_bkt(bkt);
      if (bkt != &wheel[ws_i]) bkt_pool.free(bkt);
      bkt = _tmp_next;
    }

    wheel[ws_i].last = &wheel[ws_i];  // Reset last pointer
  }

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
  const double freq_ghz;         ///< TSC freq, used only for us/tsc conversion
  const double wslot_width;      ///< Time-granularity of a slot
  const size_t wslot_width_tsc;  ///< Time-granularity in TSC units
  const double horizon;          ///< Timespan of one wheel rotation
  const size_t horizon_tsc;      ///< Horizon in TSC units
  const size_t num_wslots;
  HugeAlloc *huge_alloc;

  wheel_bkt_t *wheel;
  size_t cur_wslot;
  MemPool<wheel_bkt_t> bkt_pool;
  size_t debug_max_user_tsc;  ///< Max TSC ever provided by the caller

 public:
  std::queue<wheel_ent_t> ready_queue;
};
}

#endif  // ERPC_TIMING_WHEEL_H
