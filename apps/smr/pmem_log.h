/**
 * @file pmem_log.h
 * @brief Implementation of a log for persistent memory
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
extern "C" {
#include <raft/raft.h>
}
#include <hdr/hdr_histogram.h>
#include "../apps_common.h"

// A persistent memory log that stores objects of type T
template <class T>
class PmemLog {
 private:
  static constexpr bool kMeasureLatency = true;
  static constexpr int64_t kLatencyNsecMin = 1;        // Min = 1 ns
  static constexpr int64_t kLatencyNsecMax = 1000000;  // Min = 10 us

  // The latency reported by the HDR histogram will be precise within:
  // * 1 ns if the sample is less than 100 ns
  // * 10 ns if the sample is less than 1000 ns
  // * ...
  static constexpr size_t kLatencyNsecPrecision = 2;

  const std::string log_file;
  const double freq_ghz;

  // Volatile records
  struct {
    uint8_t *buf;         // The start of the mapped file
    size_t mapped_len;    // Length of the mapped log file
    size_t num_entries;   // Number of entries in the log
    T *log_entries_base;  // Log entries in the file
  } v;

  // Persistent metadata records
  struct {
    static_assert(sizeof(raft_node_id_t) == 4, "");
    static_assert(sizeof(raft_term_t) == 8, "");

    // This is a hack. In __raft_persist_term, we must atomically commit
    // both the term and the vote. raft_term_t is eight bytes, so the
    // combined size (12 B) exceeds the atomic write length (8 B). This is
    // simplified by shrinking the term to 4 B, and atomically doing an
    // 8-byte write to both \p term and \p voted_for.
    uint32_t *term;             // The latest term the server has seen
    raft_node_id_t *voted_for;  // Node that received vote in current term

    size_t *num_entries;  // Record for number of log entries
  } p;

  hdr_histogram *nsec_hdr;  // Statistics for latency

 public:
  PmemLog() {}

  PmemLog(std::string log_file, double freq_ghz)
      : log_file(log_file), freq_ghz(freq_ghz) {
    int is_pmem;
    v.buf = reinterpret_cast<uint8_t *>(
        pmem_map_file(log_file.c_str(), 0 /* length */, 0 /* flags */, 0666,
                      &v.mapped_len, &is_pmem));

    erpc::rt_assert(v.buf != nullptr,
                    "pmem_map_file() failed. " + std::string(strerror(errno)));
    erpc::rt_assert(v.mapped_len >= GB(32), "Raft log too short");
    erpc::rt_assert(is_pmem == 1, "Raft log file is not pmem");

    v.num_entries = 0;

    // Initialize persistent metadata pointers and reset them to zero
    uint8_t *cur = v.buf;
    p.term = reinterpret_cast<uint32_t *>(cur);
    cur += sizeof(uint32_t);
    p.voted_for = reinterpret_cast<raft_node_id_t *>(cur);
    cur += sizeof(raft_node_id_t);
    p.num_entries = reinterpret_cast<size_t *>(cur);
    cur += sizeof(size_t);
    pmem_memset_persist(v.buf, 0, static_cast<size_t>(cur - v.buf));

    v.log_entries_base = reinterpret_cast<T *>(cur);

    // Raft log entries start from index 1, so insert a garbage entry. This will
    // never be accessed, so a garbage entry is fine.
    append(T());

    if (kMeasureLatency) {
      int ret = hdr_init(kLatencyNsecMin, kLatencyNsecMax,
                         kLatencyNsecPrecision, &nsec_hdr);
      erpc::rt_assert(ret == 0);
    }
  }

  // Truncate the log so that the new size is \p num_entries
  void truncate(size_t num_entries) {
    v.num_entries = num_entries;
    pmem_memcpy_persist(p.num_entries, &v.num_entries, sizeof(v.num_entries));
  }

  void pop() { truncate(v.num_entries - 1); }

  void append(const T &entry) {
    size_t cycles_start = kMeasureLatency ? erpc::rdtsc() : 0;

    // First, update data
    T *p_log_entry_ptr = &v.log_entries_base[v.num_entries];
    pmem_memcpy_persist(p_log_entry_ptr, &entry, sizeof(T));

    // Second, update tail
    v.num_entries++;
    pmem_memcpy_persist(p.num_entries, &v.num_entries, sizeof(v.num_entries));

    if (kMeasureLatency) {
      size_t ns = erpc::to_nsec(erpc::rdtsc() - cycles_start, freq_ghz);
      if (unlikely(ns > kLatencyNsecMax)) {
        fprintf(stderr, "%zu ns is larger than expected pmem latency.\n", ns);
      }
      hdr_record_value(nsec_hdr, static_cast<int64_t>(ns));
    }
  }

  size_t get_num_entries() const { return v.num_entries; }

  std::string get_lat_str_and_reset() {
    if (kMeasureLatency) return "N/A";

    char msg[1000];
    sprintf(msg, "%zu ns 50%%, %zu ns 99%%, %zu ns 99.9%%",
            hdr_value_at_percentile(nsec_hdr, 50),
            hdr_value_at_percentile(nsec_hdr, 99),
            hdr_value_at_percentile(nsec_hdr, 99.9));
    hdr_reset(nsec_hdr);

    return std::string(msg);
  }

  void persist_vote(raft_node_id_t voted_for) {
    pmem_memcpy_persist(p.voted_for, &voted_for, sizeof(voted_for));
  }

  void persist_term(raft_term_t term, raft_node_id_t voted_for) {
    erpc::rt_assert(term < UINT32_MAX, "Term too large");
    erpc::rt_assert(reinterpret_cast<uint8_t *>(p.voted_for) ==
                    reinterpret_cast<uint8_t *>(p.term) + 4);

    // 8-byte atomic commit
    uint32_t to_persist[2];
    to_persist[0] = term;
    to_persist[1] = static_cast<uint32_t>(voted_for);
    pmem_memcpy_persist(p.term, &to_persist, sizeof(size_t));
  }
};
