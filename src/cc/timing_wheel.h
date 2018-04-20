/**
 * @file timing_wheel.h
 * @brief Timing wheel implementation from Carousel [SIGCOMM 17]
 * Units: Microseconds or TSC for time, bytes/sec for throughput
 */

#ifndef ERPC_TIMING_WHEEL_H
#define ERPC_TIMING_WHEEL_H

#include <iomanip>
#include <queue>
#include "cc/timely.h"
#include "common.h"
#include "sm_types.h"
#include "sslot.h"
#include "util/mempool.h"
#include "wheel_record.h"

namespace erpc {

static constexpr size_t kWheelBucketCap = 4;     ///< Wheel entries per bucket
static constexpr double kWheelSlotWidthUs = .2;  ///< Duration per wheel slot
static constexpr bool kWheelRecord = false;      ///< Fast-record wheel actions

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
  HugeAlloc *huge_alloc;
};

class TimingWheel {
 public:
  TimingWheel(timing_wheel_args_t args)
      : mtu(args.mtu),
        freq_ghz(args.freq_ghz),
        wslot_width_tsc(us_to_cycles(kWheelSlotWidthUs, freq_ghz)),
        horizon_us(1000000 * (kSessionCredits * mtu) / Timely::kMinRate),
        horizon_tsc(us_to_cycles(horizon_us, freq_ghz)),
        num_wslots(1 + round_up(horizon_us / kWheelSlotWidthUs)),
        huge_alloc(args.huge_alloc),
        bkt_pool(huge_alloc) {
    rt_assert(num_wslots > 10, "Too few wheel slots");
    rt_assert(num_wslots < 10000000, "Too many wheel slots");

    // wheel_buffer is leaked, and deleted later with the allocator
    Buffer wheel_buffer = huge_alloc->alloc_raw(
        num_wslots * sizeof(wheel_bkt_t), DoRegister::kFalse);
    rt_assert(wheel_buffer.buf != nullptr, "Failed to allocate wheel");

    size_t base_tsc = rdtsc();
    wheel = reinterpret_cast<wheel_bkt_t *>(wheel_buffer.buf);
    for (size_t ws_i = 0; ws_i < num_wslots; ws_i++) {
      reset_bkt(&wheel[ws_i]);
      wheel[ws_i].tx_tsc = base_tsc + (ws_i + 1) * wslot_width_tsc;
      wheel[ws_i].last = &wheel[ws_i];
    }
  }

  /// Move entries from all wheel slots older than reap_tsc to the ready queue.
  /// The max_tsc of all these wheel slots is advanced.
  /// This function must be called with non-decreasing values of reap_tsc
  void reap(size_t reap_tsc) {
    while (wheel[cur_wslot].tx_tsc <= reap_tsc) {
      reap_wslot(cur_wslot);
      wheel[cur_wslot].tx_tsc += (wslot_width_tsc * num_wslots);

      cur_wslot++;
      if (cur_wslot == num_wslots) cur_wslot = 0;
    }
  }

  /**
   * @brief Add an entry for transmission at an absolute timestamp.
   *
   * Even if the entry falls in the "current" wheel slot, we must not place it
   * entry directly in the ready queue. Doing so can reorder this entry before
   * prior entries in the current wheel slot that have not been reaped.
   *
   * @param ent The wheel entry to add
   * @param ref_tsc A recent timestamp that was used to compute \p abs_tx_tsc
   * using the sending rate.
   * @param abs_tx_tsc The desired absolute timestamp for packet transmission
   */
  inline void insert(const wheel_ent_t &ent, size_t ref_tsc,
                     size_t abs_tx_tsc) {
    assert(abs_tx_tsc >= ref_tsc);
    assert(abs_tx_tsc - ref_tsc <= horizon_tsc);  // Horizon definition

    reap(ref_tsc);  // Advance the wheel to a recent time
    assert(wheel[cur_wslot].tx_tsc > ref_tsc);

    size_t dst_wslot;
    if (abs_tx_tsc <= wheel[cur_wslot].tx_tsc) {
      dst_wslot = cur_wslot;
    } else {
      size_t wslot_delta =
          1 + (abs_tx_tsc - wheel[cur_wslot].tx_tsc) / wslot_width_tsc;
      assert(wslot_delta < num_wslots);

      dst_wslot = cur_wslot + wslot_delta;
      if (dst_wslot >= num_wslots) dst_wslot -= num_wslots;
    }

    if (kWheelRecord) record_vec.emplace_back(ent.pkt_num, abs_tx_tsc);
    insert_into_wslot(dst_wslot, ent);
  }

  /// Roll the wheel forward until it catches up with current time. Hopefully
  /// this is needed only during initialization.
  void catchup() {
    while (wheel[cur_wslot].tx_tsc < rdtsc()) reap(rdtsc());
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
  /// is reset and its chained buckets are returned to the pool.
  void reap_wslot(size_t ws_i) {
    wheel_bkt_t *bkt = &wheel[ws_i];
    while (bkt != nullptr) {
      for (size_t i = 0; i < bkt->num_entries; i++) {
        ready_queue.push(bkt->entry[i]);
        if (kWheelRecord) record_vec.emplace_back(bkt->entry[i].pkt_num);
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
  const size_t wslot_width_tsc;  ///< Time-granularity in TSC units
  const double horizon_us;       ///< Timespan of one wheel rotation
  const size_t horizon_tsc;      ///< Horizon in TSC units
  const size_t num_wslots;
  HugeAlloc *huge_alloc;

  wheel_bkt_t *wheel;
  size_t cur_wslot = 0;
  MemPool<wheel_bkt_t> bkt_pool;

 public:
  std::vector<wheel_record_t> record_vec;  ///< Used only with kWheelRecord
  std::queue<wheel_ent_t> ready_queue;
};
}

#endif  // ERPC_TIMING_WHEEL_H
