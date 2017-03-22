#ifndef ERPC_TSC_H
#define ERPC_TSC_H

#include <stdint.h>

namespace ERpc {

/// Return the TSC
static inline uint64_t rdtsc() {
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return (rdx << 32) | rax;
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to seconds
static double to_sec(uint64_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000000000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to msec
static double to_msec(uint64_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to usec
static double to_usec(uint64_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to nsec
static double to_nsec(uint64_t cycles, double freq_ghz) {
  return (cycles / freq_ghz);
}

}  /// End ERpc

#endif  // ERPC_TSC_H
