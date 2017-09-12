#pragma once
#ifndef MICA_TABLE_FIXEDTABLE_H_
#define MICA_TABLE_FIXEDTABLE_H_

#include <cstdio>
#include "mica/table/table.h"
#include "mica/util/config.h"
#include "mica/util/memcpy.h"
#include "mica/util/safe_cast.h"
#include "mica/util/barrier.h"
#include "mica/alloc/hrd_alloc.h"

// FixedTable: Maps fixed-size keys to fixed-size (multiple of 8B) values

// Configuration file entries for FixedTable:
//
//  * item_count (integer): The approximate number of items to store in the
//    table.
//  * extra_collision_avoidance (float): The amount of additional memory to
//    resolve excessive hash collisions as a fraction of the main hash table.
//  * numa_node (integer): The ID of the NUMA node to store the data.
namespace mica {
namespace table {
struct BasicFixedTableConfig {
  static constexpr size_t kBucketCap = 7;

  // Be verbose.
  static constexpr bool kVerbose = false;

  // Collect fine-grained statistics accessible via print_stats() and
  // reset_stats().
  static constexpr bool kCollectStats = false;

  // Do fetch and add only if the 1st 64-bit word of the value is even
  static constexpr bool kFetchAddOnlyIfEven = true;

  typedef ::mica::alloc::HrdAlloc Alloc;
};

template <class StaticConfig = BasicFixedTableConfig>
class FixedTable {
 public:
  std::string name;	// Name of the table

  // if @is_primary is false, the datastore is allowed to execute only
  // set_locked(), set(), del() operations. In this mode, for set() and del(),
  // it will not verify that the key's bucket is locked. (At primaries, these
  // operations verify that bucket was pre-locked during the transaction execute
  // phase.)
  typedef typename StaticConfig::Alloc Alloc;
  typedef uint64_t ft_key_t; // FixedTable key type

  static constexpr uint64_t kFtInvalidKey = 0xffffffffffffffffull;

  // fixedtable_impl/init.h
  FixedTable(const ::mica::util::Config& config, size_t val_size,
             int bkt_shm_key, Alloc* alloc, bool is_primary);
  ~FixedTable();

  void reset();

  // fixedtable_impl/bucket.h
  void print_bucket_occupancy();
  double get_locked_bkt_fraction();

  // fixedtable_impl/get.h
  Result get(uint32_t caller_id, uint64_t key_hash, ft_key_t key,
             uint64_t *out_timestamp, char* out_value) const;

  // fixedtable_impl/lock_bkt_and_get.h
  Result lock_bkt_and_get(uint32_t caller_id, uint64_t key_hash, ft_key_t key,
                          uint64_t *out_timestamp, char *value);

  // fixedtable_impl/lock_bkt_for_ins.h
  Result lock_bkt_for_ins(uint32_t caller_id, uint64_t key_hash, ft_key_t key,
                          uint64_t *out_timestamp);

  // fixedtable_impl/unlock_bkt.h
  Result unlock_bucket_hash(uint32_t caller_id, uint64_t key_hash);

  // fixedtable_impl/lock_bkt.h
  Result lock_bucket_hash(uint32_t caller_id, uint64_t key_hash);

  // fixedtable_impl/set.h
  Result set(uint32_t caller_id, uint64_t key_hash, ft_key_t key,
             const char* value);

  // fixedtable_impl/set_spinlock.h - local use only
  Result set_spinlock(uint32_t caller_id, uint64_t key_hash, ft_key_t key,
                      const char* value);

  // fixedtable_impl/del.h
  Result del(uint32_t caller_id, uint64_t key_hash, ft_key_t key);

  // fixedtable_impl/prefetch.h
  void prefetch_table(uint64_t key_hash) const;

  // fixedtable_impl/info.h
  void print_buckets() const;
  void print_stats() const;
  void reset_stats(bool reset_count);

 private:
  // Bucket configuration
  static constexpr uint32_t kNumLocksBits = 5;
  static constexpr uint32_t kMaxNumLocks = ((1u << kNumLocksBits) - 1);
  static_assert(kMaxNumLocks > StaticConfig::kBucketCap, "");
  
  static constexpr uint32_t kCallerIdBits = (32 - kNumLocksBits);
  static constexpr uint32_t kMaxCallerId = ((1u << kCallerIdBits) - 1);
  static constexpr uint32_t kInvalidCallerId = kMaxCallerId;

  // 3-bit number equal to HOTS_TS_TIMESTAMP (hots.h)
  static constexpr uint64_t kTimestampCanary = 5;

  // To keep the value size runtime-configurable, the value array is not
  // included in the Bucket struct. In the allocated memory, the value array
  // for a Bucket is adjacent to it. So, the size of each logical bucket is
  // (sizeof(Bucket) + val_size * kBucketCap).
  // To locate the ith Bucket, use the get_bucket(i) function.
  struct Bucket {
    uint32_t locker_id :kCallerIdBits;
    uint32_t num_locks :kNumLocksBits; // Number of locks held by @locker_id

    uint32_t next_extra_bucket_index;  // 1-base; 0 = no extra bucket
    uint64_t timestamp;
    ft_key_t key_arr[StaticConfig::kBucketCap];
  };

  static_assert(sizeof(Bucket) == 2 * sizeof(uint64_t) +
    StaticConfig::kBucketCap * sizeof(ft_key_t), "");

  size_t bkt_size_with_val;	// Size of the buckets with value

  struct ExtraBucketFreeList {
    uint8_t lock;
    uint32_t head;  // 1-base; 0 = no extra bucket
  };

  struct Stats {
    size_t count;
    size_t set_new;
    size_t get_found;
    size_t get_locked;
    size_t get_notfound;
    size_t test_found;
    size_t test_notfound;
    size_t delete_found;
    size_t delete_notfound;
  };

  // fixedtable_impl/bucket.h
  uint32_t calc_bucket_index(uint64_t key_hash) const;
  uint8_t* get_value(const Bucket *bucket, size_t item_index) const;
  const Bucket* get_bucket(uint32_t bucket_index) const;
  Bucket* get_bucket(uint32_t bucket_index);
  static bool has_extra_bucket(const Bucket* bucket);
  const Bucket* get_extra_bucket(uint32_t extra_bucket_index) const;
  Bucket* get_extra_bucket(uint32_t extra_bucket_index);
  bool alloc_extra_bucket(Bucket* bucket);
  void free_extra_bucket(Bucket* bucket);
  void fill_hole(Bucket* bucket, size_t unused_item_index);
  size_t get_empty(Bucket* bucket, Bucket** located_bucket);
  size_t find_item_index(const Bucket* bucket, ft_key_t key,
                         const Bucket** located_bucket) const;
  size_t find_item_index(Bucket* bucket, ft_key_t key, Bucket** located_bucket);

  // fixedtable_impl/info.h
  void print_bucket(const Bucket* bucket) const;
  void stat_inc(size_t Stats::*counter) const;
  void stat_dec(size_t Stats::*counter) const;

  // fixedtable_impl/item.h
  void set_item(Bucket *located_bucket, size_t item_index,
                       ft_key_t key, const char* value);

  // fixedtable_impl/lock.h
  bool lock_bucket_ptr(uint32_t caller_id, Bucket* bucket);
  void unlock_bucket_ptr(uint32_t caller_id, Bucket* bucket);
  void lock_extra_bucket_free_list();
  void unlock_extra_bucket_free_list();
  uint64_t read_timestamp(const Bucket* bucket) const;
  bool is_locked(uint64_t timestamp) const;
  bool is_unlocked(uint64_t timestamp) const;

  ::mica::util::Config config_;
public:
  size_t val_size;	// Size of each value
  int bkt_shm_key;	// User-defined SHM key used for bucket memory
  Alloc* alloc_;
  bool is_primary;

private:
  Bucket* buckets_ = NULL;
  Bucket* extra_buckets_ = NULL;  // = (buckets + num_buckets); extra_buckets[0]
                                  // is not used because index 0 indicates "no
                                  // more extra bucket"

  uint32_t num_buckets_;
  uint32_t num_buckets_mask_;
  uint32_t num_extra_buckets_;

  // Padding to separate static and dynamic fields.
  char padding0[128];

  ExtraBucketFreeList extra_bucket_free_list_;

  mutable Stats stats_;
} __attribute__((aligned(128)));  // To prevent false sharing caused by
                                  // adjacent cacheline prefetching.
}
}

#include "mica/table/fixedtable_impl/lock.h"
#include "mica/table/fixedtable_impl/bucket.h"
#include "mica/table/fixedtable_impl/item.h"
#include "mica/table/fixedtable_impl/info.h"
#include "mica/table/fixedtable_impl/init.h"

// Datapath operations
#include "mica/table/fixedtable_impl/del.h"
#include "mica/table/fixedtable_impl/set.h"
#include "mica/table/fixedtable_impl/set_spinlock.h"
#include "mica/table/fixedtable_impl/get.h"
#include "mica/table/fixedtable_impl/lock_bkt_and_get.h"
#include "mica/table/fixedtable_impl/lock_bkt_for_ins.h"
#include "mica/table/fixedtable_impl/lock_bkt.h"
#include "mica/table/fixedtable_impl/unlock_bkt.h"
#include "mica/table/fixedtable_impl/prefetch.h"

#endif
