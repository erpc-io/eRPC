/**
 * @file mica-pmem.h
 * @brief Simple MICA-style persistent KV store. Header-only, no dependencies.
 * @author Anuj Kalia
 */
#pragma once

#include <assert.h>
#include <city.h>
#include <libpmem.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace pmica {

static constexpr size_t kSlotsPerBucket = 5;
static constexpr size_t kMaxBatchSize = 16;
static constexpr size_t kNumRedoLogEntries = kMaxBatchSize * 8;
static constexpr bool kPMicaVerbose = false;

/// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition, std::string throw_str) {
  if (!condition) throw std::runtime_error(throw_str);
}

// Aligns 64b input parameter to the next power of 2
static uint64_t rte_align64pow2(uint64_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;

  return v + 1;
}

template <typename T>
static constexpr bool is_power_of_two(T x) {
  return x && ((x & T(x - 1)) == 0);
}

template <uint64_t PowerOfTwoNumber, typename T>
static constexpr T roundup(T x) {
  static_assert(is_power_of_two(PowerOfTwoNumber),
                "PowerOfTwoNumber must be a power of 2");
  return ((x) + T(PowerOfTwoNumber - 1)) & (~T(PowerOfTwoNumber - 1));
}

template <typename Key, typename Value>
class HashMap {
 public:
  enum class State : size_t { kEmpty = 0, kFull, kDelete };  // Slot state

  class Slot {
   public:
    Key key;
    Value value;

    Slot(Key key, Value value) : key(key), value(value) {}
    Slot() {}
  };

  struct Bucket {
    size_t unused[1];
    size_t next_extra_bucket_idx;  // 1-base; 0 = no extra bucket
    Slot slot_arr[kSlotsPerBucket];
  };

  // A redo log entry is committed iff its sequence number is less than or equal
  // to the committed_seq_num.
  class RedoLogEntry {
   public:
    size_t seq_num;  // Sequence number of this entry. Zero is invalid.
    Key key;
    Value value;

    char padding[128 - (sizeof(seq_num) + sizeof(key) + sizeof(value))];

    RedoLogEntry(size_t seq_num, const Key* key, const Value* value)
        : seq_num(seq_num), key(*key), value(*value) {}
    RedoLogEntry() {}
  };

  class RedoLog {
   public:
    RedoLogEntry entries[kNumRedoLogEntries];
    size_t committed_seq_num;
  };

  // Initialize the persistent buffer for this hash table. This modifies only
  // mapped_len.
  uint8_t* map_pbuf(size_t& _mapped_len) const {
    int is_pmem;
    uint8_t* pbuf = reinterpret_cast<uint8_t*>(
        pmem_map_file(pmem_file.c_str(), 0 /* length */, 0 /* flags */, 0666,
                      &_mapped_len, &is_pmem));

    rt_assert(pbuf != nullptr, "pmem_map_file() failed for " + pmem_file);
    rt_assert(reinterpret_cast<size_t>(pbuf) % 256 == 0, "pbuf not aligned");

    if (mapped_len - file_offset < reqd_space) {
      fprintf(stderr,
              "pmem file too small. %.2f GB required for hash table "
              "(%zu buckets, bucket size = %zu), but only %.2f GB available\n",
              reqd_space * 1.0 / (1ull << 30), num_total_buckets,
              sizeof(Bucket), mapped_len * 1.0 / (1ull << 30));
    }
    rt_assert(is_pmem == 1, "File is not pmem");

    return pbuf + file_offset;
  }

  // Allocate a hash table with space for \p num_keys keys, and chain overflow
  // room for \p overhead_fraction of the keys
  //
  // The hash table is stored in pmem_file at \p file_offset
  HashMap(std::string pmem_file, size_t file_offset, size_t num_requested_keys,
          double overhead_fraction)
      : pmem_file(pmem_file),
        file_offset(file_offset),
        num_requested_keys(num_requested_keys),
        overhead_fraction(overhead_fraction),
        num_regular_buckets(
            rte_align64pow2(num_requested_keys / kSlotsPerBucket)),
        num_extra_buckets(num_regular_buckets * overhead_fraction),
        num_total_buckets(num_regular_buckets + num_extra_buckets),
        reqd_space(get_required_bytes(num_requested_keys, overhead_fraction)),
        invalid_key(get_invalid_key()) {
    rt_assert(num_requested_keys >= kSlotsPerBucket, ">=1 buckets needed");
    rt_assert(file_offset % 256 == 0, "Unaligned file offset");

    printf("Space required = %.4f GB, key capacity = %.4f M. Bkt size = %zu\n",
           reqd_space * 1.0 / (1ull << 30), get_key_capacity() / 1000000.0,
           sizeof(Bucket));

    pbuf = map_pbuf(mapped_len);

    // Set the committed seq num, and all redo log entry seq nums to zero.
    redo_log = reinterpret_cast<RedoLog*>(pbuf);
    pmem_memset_persist(redo_log, 0, sizeof(RedoLog));

    // Initialize buckets
    size_t bucket_offset = roundup<256>(sizeof(RedoLog));
    buckets_ = reinterpret_cast<Bucket*>(&pbuf[bucket_offset]);

    // extra_buckets_[0] is the actually the last regular bucket. extra_buckets_
    // is indexed starting from one, so the last regular bucket is never used
    // as an extra bucket.
    extra_buckets_ =
        reinterpret_cast<Bucket*>(reinterpret_cast<uint8_t*>(buckets_) +
                                  ((num_regular_buckets - 1) * sizeof(Bucket)));

    // Initialize the free list of extra buckets
    printf("Initializing extra buckets freelist (%zu buckets)\n",
           num_extra_buckets);
    extra_bucket_free_list.resize(num_extra_buckets);
    for (size_t i = 0; i < num_extra_buckets; i++) {
      extra_bucket_free_list[i] = i + 1;
    }

    reset();
  }

  ~HashMap() {
    if (pbuf != nullptr) pmem_unmap(pbuf - file_offset, mapped_len);
  }

  /// Return the total bytes required for a table with \p num_requested_keys
  /// keys and \p overhead_fraction extra buckets. The returned space includes
  /// redo log. The returned space is aligned to 256 bytes.
  static size_t get_required_bytes(size_t num_requested_keys,
                                   double overhead_fraction) {
    size_t num_regular_buckets =
        rte_align64pow2(num_requested_keys / kSlotsPerBucket);
    size_t num_extra_buckets = num_regular_buckets * overhead_fraction;
    size_t num_total_buckets = num_regular_buckets + num_extra_buckets;

    size_t tot_size = sizeof(RedoLog) + num_total_buckets * sizeof(Bucket);
    return roundup<256>(tot_size);
  }

  static size_t get_hash(const Key* k) {
    return CityHash64(reinterpret_cast<const char*>(k), sizeof(Key));
  }

  static Key get_invalid_key() {
    Key ret;
    memset(&ret, 0, sizeof(ret));
    return ret;
  }

  // Convert a key to a 64-bit value for printing
  static size_t to_size_t_key(const Key* k) {
    return *reinterpret_cast<const size_t*>(k);
  }

  // Convert a value to a 64-bit value for printing
  static size_t to_size_t_val(const Value* v) {
    return *reinterpret_cast<const size_t*>(v);
  }

  // Initialize the contents of both regular and extra buckets
  void reset() {
    double GB_to_memset =
        num_total_buckets * sizeof(Bucket) * 1.0 / (1ull << 30);
    printf("Resetting hash table. This might take a while (~ %.1f seconds)\n",
           GB_to_memset / 3.0);

    // We need to achieve the following:
    //  * bucket.slot[i].key = invalid_key;
    //  * bucket.next_extra_bucket_idx = 0;
    // pmem_memset_persist() uses SIMD, so it's faster
    pmem_memset_persist(&buckets_[0], 0, num_total_buckets * sizeof(Bucket));
  }

  void prefetch(uint64_t key_hash) const {
    if (!opts.prefetch) return;

    size_t bucket_index = key_hash & (num_regular_buckets - 1);
    const Bucket* bucket = &buckets_[bucket_index];

    // Prefetching two cache lines seems to works best
    __builtin_prefetch(bucket, 0, 0);
    __builtin_prefetch(reinterpret_cast<const char*>(bucket) + 64, 0, 0);
  }

  // Find a bucket (\p located_bucket) and slot index (return value) in the
  // chain starting from \p bucket that contains \p key. If no such bucket is
  // found, return kSlotsPerBucket.
  size_t find_item_index(Bucket* bucket, const Key* key,
                         Bucket** located_bucket) const {
    Bucket* current_bucket = bucket;

    while (true) {
      for (size_t i = 0; i < kSlotsPerBucket; i++) {
        if (current_bucket->slot_arr[i].key != *key) continue;

        *located_bucket = current_bucket;
        return i;
      }

      if (current_bucket->next_extra_bucket_idx == 0) break;
      current_bucket = &extra_buckets_[current_bucket->next_extra_bucket_idx];
    }

    return kSlotsPerBucket;
  }

  // Batched operation that takes in both GETs and SETs. When this function
  // returns, all SETs are persistent in the log.
  //
  // For GETs, value_arr slots contain results. For SETs, they contain the value
  // to SET. This version of batch_op_drain assumes that the caller hash already
  // issued prefetches.
  void batch_op_drain_helper(bool* is_set, size_t* keyhash_arr,
                             const Key** key_arr, Value** value_arr,
                             bool* success_arr, size_t n) {
    bool all_gets = true;
    for (size_t i = 0; i < n; i++) {
      if (is_set[i]) {
        all_gets = false;
        RedoLogEntry v_rle(cur_sequence_number, key_arr[i], value_arr[i]);

        // Drain all pending writes to the table when we reuse log entries
        if (cur_sequence_number % kNumRedoLogEntries == 0) pmem_drain();

        RedoLogEntry& p_rle =
            redo_log->entries[cur_sequence_number % kNumRedoLogEntries];

        if (opts.redo_batch) {
          // We will write to the committed sequence number later
          pmem_memcpy_nodrain(&p_rle, &v_rle, sizeof(v_rle));
        } else {
          pmem_memcpy_persist(&p_rle, &v_rle, sizeof(v_rle));
          pmem_memcpy_persist(&redo_log->committed_seq_num,
                              &cur_sequence_number, sizeof(size_t));
        }

        cur_sequence_number++;  // Just the in-memory copy
      }
    }

    if (opts.redo_batch && !all_gets) {
      // This is needed only if redo log batching is enabled
      pmem_drain();  // Block until the redo log entries are persistent
      pmem_memcpy_persist(&redo_log->committed_seq_num, &cur_sequence_number,
                          sizeof(size_t));
    }

    for (size_t i = 0; i < n; i++) {
      if (is_set[i]) {
        success_arr[i] = set_nodrain(keyhash_arr[i], key_arr[i], value_arr[i]);
      } else {
        success_arr[i] = get(keyhash_arr[i], key_arr[i], value_arr[i]);
      }
    }
  }

  // Batched operation that takes in both GETs and SETs. When this function
  // returns, all SETs are persistent in the log.
  //
  // For GETs, value_arr slots contain results. For SETs, they contain the value
  // to SET. This version of batch_op_drain issues prefetches for the caller.
  inline void batch_op_drain(bool* is_set, const Key** key_arr,
                             Value** value_arr, bool* success_arr, size_t n) {
    size_t keyhash_arr[kMaxBatchSize];

    for (size_t i = 0; i < n; i++) {
      keyhash_arr[i] = get_hash(key_arr[i]);
      prefetch(keyhash_arr[i]);
    }

    batch_op_drain_helper(is_set, keyhash_arr, key_arr, value_arr, success_arr,
                          n);
  }

  bool get(const Key* key, Value* out_value) const {
    assert(*key != invalid_key);
    return get(get_hash(key), key, out_value);
  }

  bool get(uint64_t key_hash, const Key* key, Value* out_value) const {
    assert(*key != invalid_key);

    size_t bucket_index = key_hash & (num_regular_buckets - 1);
    Bucket* bucket = &buckets_[bucket_index];

    Bucket* located_bucket;
    size_t item_index = find_item_index(bucket, key, &located_bucket);

    if (kPMicaVerbose) {
      printf("get key %zu (#%zx), located bucket %p, index %zu, found = %s\n",
             to_size_t_key(key), key_hash, static_cast<void*>(located_bucket),
             item_index, (item_index == kSlotsPerBucket) ? "yes" : "no");
    }

    if (item_index == kSlotsPerBucket) return false;

    *out_value = located_bucket->slot_arr[item_index].value;
    return true;
  }

  bool alloc_extra_bucket(Bucket* bucket) {
    if (extra_bucket_free_list.empty()) return false;
    size_t extra_bucket_index = extra_bucket_free_list.back();
    assert(extra_bucket_index >= 1);
    extra_bucket_free_list.pop_back();
    if (kPMicaVerbose) {
      printf(" allocated extra bucket %zu\n", extra_bucket_index);
    }

    // This is an eight-byte operation, so no need in redo log
    pmem_memcpy_persist(&bucket->next_extra_bucket_idx, &extra_bucket_index,
                        sizeof(extra_bucket_index));
    return true;
  }

  // Traverse the bucket chain starting at \p bucket, and fill in \p
  // located_bucket such that located_bucket contains an empty slot. The empty
  // slot index is the return value. This might add a new extra bucket to the
  // chain.
  //
  // Return kSlotsPerBucket if an empty slot is not possible
  size_t get_empty(Bucket* bucket, Bucket** located_bucket) {
    Bucket* current_bucket = bucket;
    while (true) {
      for (size_t i = 0; i < kSlotsPerBucket; i++) {
        if (current_bucket->slot_arr[i].key == invalid_key) {
          *located_bucket = current_bucket;
          return i;
        }
      }
      if (current_bucket->next_extra_bucket_idx == 0) break;
      current_bucket = &extra_buckets_[current_bucket->next_extra_bucket_idx];
    }

    // no space; alloc new extra_bucket
    if (alloc_extra_bucket(current_bucket)) {
      *located_bucket = &extra_buckets_[current_bucket->next_extra_bucket_idx];
      return 0;  // use the first slot (it should be empty)
    } else {
      return kSlotsPerBucket;  // No free extra bucket
    }
  }

  // Set a key-value item without a final sfence
  bool set_nodrain(const Key* key, const Value* value) {
    assert(*key != invalid_key);
    return set_nodrain(get_hash(key), key, value);
  }

  // Set a key-value item without a final sfence. If redo logging is disabled, a
  // final sfence is used.
  bool set_nodrain(uint64_t key_hash, const Key* key, const Value* value) {
    assert(*key != invalid_key);

    if (kPMicaVerbose) {
      printf("set key %zu (#%zx), value %zu. freelist sz = %zu\n",
             to_size_t_key(key), key_hash, to_size_t_val(value),
             extra_bucket_free_list.size());
    }

    size_t bucket_index = key_hash & (num_regular_buckets - 1);
    Bucket* bucket = &buckets_[bucket_index];
    Bucket* located_bucket;
    size_t item_index = find_item_index(bucket, key, &located_bucket);

    if (item_index == kSlotsPerBucket) {
      if (kPMicaVerbose) {
        printf("  not found in main bucket %p. traversing chain\n",
               static_cast<void*>(bucket));
      }
      item_index = get_empty(bucket, &located_bucket);
      if (item_index == kSlotsPerBucket) {
        if (kPMicaVerbose) {
          printf("  no empty bucket %p\n", static_cast<void*>(bucket));
        }
        return false;
      }
    }

    if (kPMicaVerbose) {
      printf("  set key %zu (#%zx), value %zu success. bucket %p, index %zu\n",
             to_size_t_key(key), key_hash, to_size_t_val(value),
             static_cast<void*>(located_bucket), item_index);
    }

    Slot s(*key, *value);
    if (opts.async_drain) {
      pmem_memcpy_nodrain(&located_bucket->slot_arr[item_index], &s, sizeof(s));
    } else {
      pmem_memcpy_persist(&located_bucket->slot_arr[item_index], &s, sizeof(s));
    }

    return true;
  }

  // Return the number of keys that can be stored in this table
  size_t get_key_capacity() const {
    return num_total_buckets * kSlotsPerBucket;
  };

  // Constructor args
  const std::string pmem_file;      // Name of the pmem file
  const size_t file_offset;         // Offset in file where the table is placed
  const size_t num_requested_keys;  // User's requested key capacity
  const double overhead_fraction;   // User's requested key capacity

  const size_t num_regular_buckets;  // Power-of-two number of main buckets
  const size_t num_extra_buckets;    // num_regular_buckets * overhead_fraction
  const size_t num_total_buckets;    // Sum of regular and extra buckets
  const size_t reqd_space;           // Total bytes needed for the table
  const Key invalid_key;

  Bucket* buckets_ = nullptr;

  // = (buckets + num_buckets); extra_buckets[0] is not used because index 0
  // indicates "no more extra buckets"
  Bucket* extra_buckets_ = nullptr;

  std::vector<size_t> extra_bucket_free_list;

  uint8_t* pbuf;      // The pmem buffer for this table
  size_t mapped_len;  // The length mapped by libpmem
  RedoLog* redo_log;
  size_t cur_sequence_number = 1;

  struct {
    bool prefetch = true;     // Software prefetching
    bool redo_batch = true;   // Redo log batching
    bool async_drain = true;  // Drain slot writes asynchronously

    void reset() {
      prefetch = true;
      redo_batch = true;
      async_drain = true;
    }
  } opts;
};

}  // namespace pmica
