#pragma once

#include "common.h"
#include "util/timer.h"

namespace erpc {

/// Used for fast recording of wheel actions for debugging
struct wheel_record_t {
  size_t record_tsc_;  ///< Timestamp at which this record was created
  bool insert_;        ///< Is this a record for a wheel insertion?
  size_t pkt_num_;     ///< The request number of the wheel entry's sslot
  size_t abs_tx_tsc_;  ///< For inserts, the requested TX timestamp

  /// Record an wheel insertion entry
  wheel_record_t(size_t pkt_num, size_t abs_tx_tsc)
      : record_tsc_(rdtsc()),
        insert_(true),
        pkt_num_(pkt_num),
        abs_tx_tsc_(abs_tx_tsc) {}

  /// Record a wheel reap entry
  wheel_record_t(size_t pkt_num)
      : record_tsc_(rdtsc()), insert_(false), pkt_num_(pkt_num) {}

  std::string to_string(size_t console_ref_tsc, double freq_ghz) {
    std::ostringstream ret;
    size_t record_us = to_usec(record_tsc_ - console_ref_tsc, freq_ghz);
    size_t abs_tx_us = to_usec(abs_tx_tsc_ - console_ref_tsc, freq_ghz);
    if (insert_) {
      ret << "[Insert for pkt" << pkt_num_ << ", at " << record_us
          << " us, abs TX " << abs_tx_us << " us]";
    } else {
      ret << "[Reap for req " << pkt_num_ << ", at " << record_us << " us]";
    }

    return ret.str();
  }
};
}  // namespace erpc
