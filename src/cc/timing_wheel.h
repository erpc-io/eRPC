/**
 * @file timing_wheel.h
 * @brief Timing wheel implementation from Carousel [SIGCOMM 17]
 * Units: Microseconds or TSC for time, bytes/sec for throughput
 *
 * SSlots scheduled for transmission in a wheel slot are stored in a chain
 * of associative buckets. Deletes do not compact the chain, so some buckets in
 * a chain may be empty or partially full.
 */

#pragma once

#include <iomanip>
#include <queue>
#include "cc/timely.h"
#include "common.h"
#include "sm_types.h"
#include "sslot.h"
#include "transport_impl/dpdk/dpdk_transport.h"
#include "transport_impl/infiniband/ib_transport.h"
#include "transport_impl/raw/raw_transport.h"
#include "util/mempool.h"
#include "wheel_record.h"

namespace erpc {

static constexpr double kWheelSlotWidthUs = .5;  ///< Duration per wheel slot
static constexpr double kWheelHorizonUs =
    1000000 * (kSessionCredits * CTransport::kMTU) / Timely::kMinRate;

// This ensures that packets for an sslot undergoing retransmission are rarely
// in the wheel. This is recommended but not required.
// static_assert(kWheelHorizonUs <= kRpcRTOUs, "");

static constexpr size_t kWheelNumWslots =
    1 + erpc::ceil(kWheelHorizonUs / kWheelSlotWidthUs);

static constexpr bool kWheelRecord = false;  ///< Fast-record wheel actions

/// One entry in a timing wheel bucket
struct wheel_ent_t {
  uint64_t sslot : 48;  ///< The things I do for perf
  uint64_t pkt_num : 16;
  wheel_ent_t(SSlot *sslot, size_t pkt_num)
      : sslot(reinterpret_cast<uint64_t>(sslot)), pkt_num(pkt_num) {}
};
static_assert(sizeof(wheel_ent_t) == 8, "");

// Handle the fact that we're not using all 64 TSC bits
static constexpr size_t kWheelBucketCap = 5;  ///< Wheel entries per bucket
static constexpr size_t kNumBktEntriesBits = 3;
static_assert((1ull << kNumBktEntriesBits) > kWheelBucketCap, "");

// TSC ticks per day = ~3 billion per second * 86400 seconds per day
// Require that rollback happens only after server lifetime
static constexpr size_t _kTscTicks = 1ull << (64 - kNumBktEntriesBits);
static_assert(_kTscTicks / (3000000000ull * 86400) > 2000, "");

struct wheel_bkt_t {
  size_t num_entries : kNumBktEntriesBits;  ///< Valid entries in this bucket

  /// Timestamp at which it is safe to transmit packets in this bucket's chain
  size_t tx_tsc : 64 - kNumBktEntriesBits;

  wheel_bkt_t *last;  ///< Last bucket in chain. Used only at the first bucket.
  wheel_bkt_t *next;  ///< Next bucket in chain
  wheel_ent_t entry[kWheelBucketCap];  ///< Space for wheel entries
};
static_assert(sizeof(wheel_bkt_t) == 64, "");

struct timing_wheel_args_t {
  double freq_ghz;
  HugeAlloc *huge_alloc;
};

class TimingWheel {
 public:
  TimingWheel(timing_wheel_args_t args)
      : freq_ghz(args.freq_ghz),
        wslot_width_tsc(us_to_cycles(kWheelSlotWidthUs, freq_ghz)),
        horizon_tsc(us_to_cycles(kWheelHorizonUs, freq_ghz)),
        huge_alloc(args.huge_alloc),
        bkt_pool(huge_alloc) {
    // wheel_buffer is leaked by the wheel, and deleted later with the allocator
    Buffer wheel_buffer = huge_alloc->alloc_raw(
        kWheelNumWslots * sizeof(wheel_bkt_t), DoRegister::kFalse);
    rt_assert(wheel_buffer.buf != nullptr,
              std::string("Failed to allocate wheel. ") +
                  HugeAlloc::alloc_fail_help_str);

    size_t base_tsc = rdtsc();
    wheel = reinterpret_cast<wheel_bkt_t *>(wheel_buffer.buf);
    for (size_t ws_i = 0; ws_i < kWheelNumWslots; ws_i++) {
      reset_bkt(&wheel[ws_i]);
      wheel[ws_i].tx_tsc = base_tsc + (ws_i + 1) * wslot_width_tsc;
      wheel[ws_i].last = &wheel[ws_i];
    }
  }

  /// Return a dummy wheel entry
  static wheel_ent_t get_dummy_ent() {
    return wheel_ent_t(reinterpret_cast<SSlot *>(0xdeadbeef), 3185);
  }

  /// Roll the wheel forward until it catches up with current time. Hopefully
  /// this is needed only during initialization.
  void catchup() {
    while (wheel[cur_wslot].tx_tsc < rdtsc()) reap(rdtsc());
  }

  /// Move entries from all wheel slots older than reap_tsc to the ready queue.
  /// The max_tsc of all these wheel slots is advanced.
  /// This function must be called with non-decreasing values of reap_tsc
  void reap(size_t reap_tsc) {
    while (wheel[cur_wslot].tx_tsc <= reap_tsc) {
      reap_wslot(cur_wslot);
      wheel[cur_wslot].tx_tsc += (wslot_width_tsc * kWheelNumWslots);

      cur_wslot++;
      if (cur_wslot == kWheelNumWslots) cur_wslot = 0;
    }
  }

  /**
   * @brief Add an entry to the wheel. This may move existing entries to the
   * wheel's ready queue.
   *
   * Even if the entry falls in the "current" wheel slot, we must not place it
   * entry directly in the ready queue. Doing so can reorder this entry before
   * prior entries in the current wheel slot that have not been reaped.
   *
   * @param ent The wheel entry to add
   * @param ref_tsc A recent timestamp
   * @param desired_tx_tsc The desired time for packet transmission
   */
  inline void insert(const wheel_ent_t &ent, size_t ref_tsc,
                     size_t desired_tx_tsc) {
    assert(desired_tx_tsc >= ref_tsc);
    assert(desired_tx_tsc - ref_tsc <= horizon_tsc);  // Horizon definition

    reap(ref_tsc);  // Advance the wheel to a recent time
    assert(wheel[cur_wslot].tx_tsc > ref_tsc);

    size_t dst_wslot;
    if (desired_tx_tsc <= wheel[cur_wslot].tx_tsc) {
      dst_wslot = cur_wslot;
    } else {
      size_t wslot_delta =
          1 + (desired_tx_tsc - wheel[cur_wslot].tx_tsc) / wslot_width_tsc;
      assert(wslot_delta < kWheelNumWslots);

      dst_wslot = cur_wslot + wslot_delta;
      if (dst_wslot >= kWheelNumWslots) dst_wslot -= kWheelNumWslots;
    }

    if (kWheelRecord) record_vec.emplace_back(ent.pkt_num, desired_tx_tsc);

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
  /// is reset and its chained buckets are returned to the pool.
  void reap_wslot(size_t ws_i) {
    wheel_bkt_t *bkt = &wheel[ws_i];
    while (bkt != nullptr) {
      for (size_t i = 0; i < bkt->num_entries; i++) {
        ready_queue.push(bkt->entry[i]);
        if (kWheelRecord) {
          record_vec.push_back(wheel_record_t(bkt->entry[i].pkt_num));
        }
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

  const double freq_ghz;         ///< TSC freq, used only for us/tsc conversion
  const size_t wslot_width_tsc;  ///< Time-granularity in TSC units
  const size_t horizon_tsc;      ///< Horizon in TSC units
  HugeAlloc *huge_alloc;

  wheel_bkt_t *wheel;
  size_t cur_wslot = 0;
  MemPool<wheel_bkt_t> bkt_pool;

 public:
  std::vector<wheel_record_t> record_vec;  ///< Used only with kWheelRecord
  std::queue<wheel_ent_t> ready_queue;
};
}  // namespace erpc
