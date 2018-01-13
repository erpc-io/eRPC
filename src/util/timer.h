/**
 * @file timer.h
 * @brief Helper functions for timers
 */

#ifndef ERPC_TIMER_H
#define ERPC_TIMER_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "common.h"
#include "math_utils.h"

namespace erpc {

/// Return the TSC
static inline size_t rdtsc() {
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return static_cast<size_t>((rdx << 32) | rax);
}

static void nano_sleep(size_t ns, double freq_ghz) {
  size_t start = rdtsc();
  size_t end = start;
  size_t upp = static_cast<size_t>(freq_ghz * ns);
  while (end - start < upp) end = rdtsc();
}

static double measure_rdtsc_freq() {
  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);
  uint64_t rdtsc_start = rdtsc();

  // Do not change this loop! The hardcoded value below depends on this loop
  // and prevents it from being optimized out.
  uint64_t sum = 5;
  for (uint64_t i = 0; i < 1000000; i++) {
    sum += i + (sum + i) * (i % sum);
  }
  rt_assert(sum == 13580802877818827968ull, "Error in RDTSC freq measurement");

  clock_gettime(CLOCK_REALTIME, &end);
  uint64_t clock_ns =
      static_cast<uint64_t>(end.tv_sec - start.tv_sec) * 1000000000 +
      static_cast<uint64_t>(end.tv_nsec - start.tv_nsec);
  uint64_t rdtsc_cycles = rdtsc() - rdtsc_start;

  double _freq_ghz = rdtsc_cycles * 1.0 / clock_ns;
  rt_assert(_freq_ghz >= 0.5 && _freq_ghz <= 5.0, "Invalid RDTSC frequency");

  return _freq_ghz;
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to seconds
static double to_sec(size_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000000000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to msec
static double to_msec(size_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to usec
static double to_usec(size_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000));
}

static size_t us_to_cycles(double us, double freq_ghz) {
  return round_up(us * 1000 * freq_ghz);
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to nsec
static double to_nsec(size_t cycles, double freq_ghz) {
  return (cycles / freq_ghz);
}

/// Return seconds elapsed since timestamp \p t0
static double sec_since(const struct timespec &t0) {
  struct timespec t1;
  clock_gettime(CLOCK_REALTIME, &t1);
  return (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1000000000.0;
}

/// Return nanoseconds elapsed since timestamp \p t0
static double ns_since(const struct timespec &t0) {
  struct timespec t1;
  clock_gettime(CLOCK_REALTIME, &t1);
  return (t1.tv_sec - t0.tv_sec) * 1000000000.0 + (t1.tv_nsec - t0.tv_nsec);
}

/// Simple time that uses RDTSC
class TscTimer {
 public:
  size_t start_tsc = 0;
  size_t tsc_sum = 0;
  size_t num_calls = 0;

  inline void start() { start_tsc = rdtsc(); }
  inline void stop() {
    tsc_sum += (rdtsc() - start_tsc);
    num_calls++;
  }

  void reset() {
    start_tsc = 0;
    tsc_sum = 0;
    num_calls = 0;
  }

  size_t avg_cycles() const { return tsc_sum / num_calls; }
  double avg_sec(double freq_ghz) const {
    return to_sec(avg_cycles(), freq_ghz);
  }

  double avg_usec(double freq_ghz) const {
    return to_usec(avg_cycles(), freq_ghz);
  }

  double avg_nsec(double freq_ghz) const {
    return to_nsec(avg_cycles(), freq_ghz);
  }
};
}  /// End erpc

#endif  // ERPC_TIMER_H
