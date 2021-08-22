/**
 * @file pmem_log.h
 * @brief Implementation of a log for persistent memory
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "../apps_common.h"
#include "common.h"

#ifdef SMR_USE_PMEM
#include <libpmem.h>
#else
// Dummy versions of libpmem functions, to avoid #ifdef SMR_USE_PMEM everywhere

void *pmem_memcpy_persist(void *, const void *, size_t) {
  erpc::rt_assert(false, "pmem not supported\n");
  return nullptr;
}

void *pmem_memset_persist(void *, int, size_t) {
  erpc::rt_assert(false, "pmem not supported\n");
  return nullptr;
}

void *pmem_map_file(const char *, size_t, int, uint32_t, size_t *, int *) {
  erpc::rt_assert(false, "pmem not supported\n");
  return nullptr;
}
#endif

extern "C" {
#include <raft.h>
}

// A persistent memory log that stores objects of type T
template <class T>
class PmemLog {
 private:
  // The machines are under different DAX configs, so use the one that works
  static constexpr const char *kPmemLogFileA = "/dev/dax12.0";
  static constexpr const char *kPmemLogFileB = "/mnt/pmem12/raft_log";

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
    // This is a hack. In the persist_term callback, we must atomically commit
    // both the term and the vote. raft_term_t can be eight bytes, so the
    // combined size (12 B) exceeds the atomic write length (8 B). This is
    // simplified by shrinking the term to 4 B, and atomically doing an
    // 8-byte write to both \p term and \p voted_for.
    uint32_t *term;             // The latest term the server has seen
    raft_node_id_t *voted_for;  // Node that received vote in current term

    size_t *num_entries;  // Record for number of log entries
  } p;

 public:
  PmemLog() {}

  PmemLog(double freq_ghz) : freq_ghz(freq_ghz) {
    int is_pmem;
    v.buf = reinterpret_cast<uint8_t *>(
        pmem_map_file(kPmemLogFileA, 0 /* length */, 0 /* flags */, 0666,
                      &v.mapped_len, &is_pmem));

    if (v.buf == nullptr) {
      v.buf = reinterpret_cast<uint8_t *>(
          pmem_map_file(kPmemLogFileB, 0 /* length */, 0 /* flags */, 0666,
                        &v.mapped_len, &is_pmem));
    }

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
  }

  // Truncate the log so that the new size is \p num_entries
  void truncate(size_t num_entries) {
    v.num_entries = num_entries;
    pmem_memcpy_persist(p.num_entries, &v.num_entries, sizeof(v.num_entries));
  }

  void pop() { truncate(v.num_entries - 1); }

  void append(const T &entry) {
    // First, update data
    T *p_log_entry_ptr = &v.log_entries_base[v.num_entries];
    pmem_memcpy_persist(p_log_entry_ptr, &entry, sizeof(T));

    // Second, update tail
    v.num_entries++;
    pmem_memcpy_persist(p.num_entries, &v.num_entries, sizeof(v.num_entries));
  }

  size_t get_num_entries() const { return v.num_entries; }

  void persist_vote(raft_node_id_t voted_for) {
    pmem_memcpy_persist(p.voted_for, &voted_for, sizeof(voted_for));
  }

  void persist_term(raft_term_t term, raft_node_id_t voted_for) {
    erpc::rt_assert(term < INT32_MAX, "Term too large for atomic pmem append");
    erpc::rt_assert(reinterpret_cast<uint8_t *>(p.voted_for) ==
                    reinterpret_cast<uint8_t *>(p.term) + 4);

    // 8-byte atomic commit
    uint32_t to_persist[2];
    to_persist[0] = static_cast<uint32_t>(term);
    to_persist[1] = static_cast<uint32_t>(voted_for);
    pmem_memcpy_persist(p.term, &to_persist, sizeof(size_t));
  }
};
