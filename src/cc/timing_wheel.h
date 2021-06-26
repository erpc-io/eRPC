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
#include "transport_impl/fake/fake_transport.h"
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
  uint64_t sslot_ : 48;  ///< The things I do for perf
  uint64_t pkt_num_ : 16;
  wheel_ent_t(SSlot *sslot, size_t pkt_num)
      : sslot_(reinterpret_cast<uint64_t>(sslot)), pkt_num_(pkt_num) {}
};
static_assert(sizeof(wheel_ent_t) == 8, "");

// Handle the fact that we're not using all 64 TSC bits
static constexpr size_t kWheelBucketCap = 5;  ///< Wheel entries per bucket
static constexpr size_t kNumBktEntriesBits = 3;
static_assert((1ull << kNumBktEntriesBits) > kWheelBucketCap, "");

// TSC ticks per day = ~3 billion per second * 86400 seconds per day
// Require that rollback happens only after server lifetime
static constexpr size_t kKTscTicks = 1ull << (64 - kNumBktEntriesBits);
static_assert(kKTscTicks / (3000000000ull * 86400) > 2000, "");

struct wheel_bkt_t {
  size_t num_entries_ : kNumBktEntriesBits;  ///< Valid entries in this bucket

  /// Timestamp at which it is safe to transmit packets in this bucket's chain
  size_t tx_tsc_ : 64 - kNumBktEntriesBits;

  wheel_bkt_t *last_;  ///< Last bucket in chain. Used only at the first bucket.
  wheel_bkt_t *next_;  ///< Next bucket in chain
  wheel_ent_t entry_[kWheelBucketCap];  ///< Space for wheel entries
};
static_assert(sizeof(wheel_bkt_t) == 64, "");

struct timing_wheel_args_t {
  double freq_ghz_;
  HugeAlloc *huge_alloc_;
};

class TimingWheel {
 public:
  TimingWheel(timing_wheel_args_t args)
      : freq_ghz_(args.freq_ghz_),
        wslot_width_tsc_(us_to_cycles(kWheelSlotWidthUs, freq_ghz_)),
        horizon_tsc_(us_to_cycles(kWheelHorizonUs, freq_ghz_)),
        huge_alloc_(args.huge_alloc_),
        bkt_pool_(huge_alloc_) {
    // wheel_buffer is leaked by the wheel, and deleted later with the allocator
    Buffer wheel_buffer = huge_alloc_->alloc_raw(
        kWheelNumWslots * sizeof(wheel_bkt_t), DoRegister::kFalse);
    rt_assert(wheel_buffer.buf_ != nullptr,
              std::string("Failed to allocate wheel. ") +
                  HugeAlloc::kAllocFailHelpStr);

    size_t base_tsc = rdtsc();
    wheel_ = reinterpret_cast<wheel_bkt_t *>(wheel_buffer.buf_);
    for (size_t ws_i = 0; ws_i < kWheelNumWslots; ws_i++) {
      reset_bkt(&wheel_[ws_i]);
      wheel_[ws_i].tx_tsc_ = base_tsc + (ws_i + 1) * wslot_width_tsc_;
      wheel_[ws_i].last_ = &wheel_[ws_i];
    }
  }

  /// Return a dummy wheel entry
  static wheel_ent_t get_dummy_ent() {
    return wheel_ent_t(reinterpret_cast<SSlot *>(0xdeadbeef), 3185);
  }

  /// Roll the wheel forward until it catches up with current time. Hopefully
  /// this is needed only during initialization.
  void catchup() {
    while (wheel_[cur_wslot_].tx_tsc_ < rdtsc()) reap(rdtsc());
  }

  /// Move entries from all wheel slots older than reap_tsc to the ready queue.
  /// The max_tsc of all these wheel slots is advanced.
  /// This function must be called with non-decreasing values of reap_tsc
  void reap(size_t reap_tsc) {
    while (wheel_[cur_wslot_].tx_tsc_ <= reap_tsc) {
      reap_wslot(cur_wslot_);
      wheel_[cur_wslot_].tx_tsc_ += (wslot_width_tsc_ * kWheelNumWslots);

      cur_wslot_++;
      if (cur_wslot_ == kWheelNumWslots) cur_wslot_ = 0;
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
    assert(desired_tx_tsc - ref_tsc <= horizon_tsc_);  // Horizon definition

    reap(ref_tsc);  // Advance the wheel to a recent time
    assert(wheel_[cur_wslot_].tx_tsc_ > ref_tsc);

    size_t dst_wslot;
    if (desired_tx_tsc <= wheel_[cur_wslot_].tx_tsc_) {
      dst_wslot = cur_wslot_;
    } else {
      size_t wslot_delta =
          1 + (desired_tx_tsc - wheel_[cur_wslot_].tx_tsc_) / wslot_width_tsc_;
      assert(wslot_delta < kWheelNumWslots);

      dst_wslot = cur_wslot_ + wslot_delta;
      if (dst_wslot >= kWheelNumWslots) dst_wslot -= kWheelNumWslots;
    }

    if (kWheelRecord) record_vec_.emplace_back(ent.pkt_num_, desired_tx_tsc);

    insert_into_wslot(dst_wslot, ent);
  }

 private:
  void insert_into_wslot(size_t ws_i, const wheel_ent_t &ent) {
    wheel_bkt_t *last_bkt = wheel_[ws_i].last_;
    assert(last_bkt->next_ == nullptr);

    assert(last_bkt->num_entries_ < kWheelBucketCap);
    last_bkt->entry_[last_bkt->num_entries_] = ent;
    last_bkt->num_entries_++;

    // If last_bkt is full, allocate a new one and make it the last
    if (last_bkt->num_entries_ == kWheelBucketCap) {
      wheel_bkt_t *new_bkt = alloc_bkt();
      last_bkt->next_ = new_bkt;
      wheel_[ws_i].last_ = new_bkt;
    }
  }

  /// Transfer all entries from a wheel slot to the ready queue. The wheel slot
  /// is reset and its chained buckets are returned to the pool.
  void reap_wslot(size_t ws_i) {
    wheel_bkt_t *bkt = &wheel_[ws_i];
    while (bkt != nullptr) {
      for (size_t i = 0; i < bkt->num_entries_; i++) {
        ready_queue_.push(bkt->entry_[i]);
        if (kWheelRecord) {
          record_vec_.push_back(wheel_record_t(bkt->entry_[i].pkt_num_));
        }
      }

      wheel_bkt_t *tmp_next = bkt->next_;

      reset_bkt(bkt);
      if (bkt != &wheel_[ws_i]) bkt_pool_.free(bkt);
      bkt = tmp_next;
    }

    wheel_[ws_i].last_ = &wheel_[ws_i];  // Reset last pointer
  }

  inline void reset_bkt(wheel_bkt_t *bkt) {
    bkt->next_ = nullptr;
    bkt->num_entries_ = 0;
  }

  wheel_bkt_t *alloc_bkt() {
    wheel_bkt_t *bkt = bkt_pool_.alloc();  // Exception if allocation fails
    reset_bkt(bkt);
    return bkt;
  }

  const double freq_ghz_;         ///< TSC freq, used only for us/tsc conversion
  const size_t wslot_width_tsc_;  ///< Time-granularity in TSC units
  const size_t horizon_tsc_;      ///< Horizon in TSC units
  HugeAlloc *huge_alloc_;

  wheel_bkt_t *wheel_;
  size_t cur_wslot_ = 0;
  MemPool<wheel_bkt_t> bkt_pool_;

 public:
  std::vector<wheel_record_t> record_vec_;  ///< Used only with kWheelRecord
  std::queue<wheel_ent_t> ready_queue_;
};
}  // namespace erpc
