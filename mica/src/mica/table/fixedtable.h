#pragma once
#ifndef MICA_TABLE_FIXEDTABLE_H_
#define MICA_TABLE_FIXEDTABLE_H_

#include <cstdio>
#include "mica/table/table.h"
#include "mica/util/barrier.h"
#include "mica/util/config.h"
#include "mica/util/memcpy.h"
#include "util/huge_alloc.h"

// FixedTable: Maps fixed-size keys to fixed-size (multiple of 8B) values

// Configuration file entries for FixedTable:
//
//  * item_count (integer): The approximate number of items to store in the
//    table.
//  * extra_collision_avoidance (float): The amount of additional memory to
//    resolve excessive hash collisions as a fraction of the main hash table.
//    Default = 0. if kEviction = true, 0.1 otherwise.
//  * concurrent_read (bool): If true, enable concurrent reads by multiple
//    threads.
//  * concurrent_write (bool): If true, enable concurrent writes by multiple
//    threads.
//  * numa_node (integer): The ID of the NUMA node to store the data.
namespace mica {
namespace table {
struct BasicFixedTableConfig {
  static constexpr size_t kBucketCap = 7;

  // Support concurrent access. The actual concurrent access is enabled by
  // concurrent_read and concurrent_write in the configuration.
  static constexpr bool kConcurrent = false;

  // Be verbose.
  static constexpr bool kVerbose = false;

  // Collect fine-grained statistics accessible via print_stats() and
  // reset_stats().
  static constexpr bool kCollectStats = false;

  static constexpr size_t kKeySize = 16;
};

template <class StaticConfig = BasicFixedTableConfig>
class FixedTable {
 public:
  struct ft_key_t {
    size_t qword[StaticConfig::kKeySize / 8];

    bool operator==(const ft_key_t& other) const {
      return memcmp(qword, other.qword, StaticConfig::kKeySize) == 0;
    }

    bool operator!=(const ft_key_t& other) const {
      return memcmp(qword, other.qword, StaticConfig::kKeySize) != 0;
    }
  };

  // fixedtable_impl/init.cc
  FixedTable(const ::mica::util::Config& config, size_t val_size,
             erpc::HugeAlloc* alloc);
  ~FixedTable();

  void reset();

  // fixedtable_impl/bucket.cc
  void print_bucket_occupancy();

  // fixedtable_impl/del.h
  Result del(uint64_t key_hash, const ft_key_t& key);

  // fixedtable_impl/get.cc
  Result get(uint64_t key_hash, const ft_key_t& key, char* out_value) const;

  // fixedtable_impl/set.cc
  Result set(uint64_t key_hash, const ft_key_t& key, const char* value);

  // fixedtable_impl/prefetch.h
  void prefetch_table(uint64_t key_hash) const;

  // fixedtable_impl/info.h
  void print_buckets() const;
  void print_stats() const;
  void reset_stats(bool reset_count);

 private:
  // To keep the value size runtime-configurable, the value array is not
  // included in the Bucket struct. In the allocated memory, the value array
  // for a Bucket is adjacent to it. So, the size of each logical bucket is
  // (sizeof(Bucket) + val_size * kBucketCap).
  // To locate the ith Bucket, use the get_bucket(i) function.
  struct Bucket {
    uint32_t version;                  // XXX: is uint32_t wide enough?
    uint32_t next_extra_bucket_index;  // 1-base; 0 = no extra bucket
    ft_key_t key_arr[StaticConfig::kBucketCap];
  };

  static_assert(sizeof(Bucket) == sizeof(uint64_t) + StaticConfig::kBucketCap *
                                                         sizeof(ft_key_t),
                "");

  size_t bkt_size_with_val;  // Size of the buckets with value

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

  // fixedtable_impl/init.cc
  ft_key_t invalid_key() const;

  // fixedtable_impl/bucket.h
  uint32_t calc_bucket_index(uint64_t key_hash) const;
  uint8_t* get_value(const Bucket* bucket, size_t item_index) const;
  const Bucket* get_bucket(uint32_t bucket_index) const;
  Bucket* get_bucket(uint32_t bucket_index);
  static bool has_extra_bucket(const Bucket* bucket);
  const Bucket* get_extra_bucket(uint32_t extra_bucket_index) const;
  Bucket* get_extra_bucket(uint32_t extra_bucket_index);
  bool alloc_extra_bucket(Bucket* bucket);
  void free_extra_bucket(Bucket* bucket);
  void fill_hole(Bucket* bucket, size_t unused_item_index);
  size_t get_empty(Bucket* bucket, Bucket** located_bucket);
  size_t find_item_index(const Bucket* bucket, const ft_key_t& key,
                         const Bucket** located_bucket) const;
  size_t find_item_index(Bucket* bucket, const ft_key_t& key,
                         Bucket** located_bucket);

  // fixedtable_impl/info.h
  void print_bucket(const Bucket* bucket) const;
  void stat_inc(size_t Stats::*counter) const;
  void stat_dec(size_t Stats::*counter) const;

  // fixedtable_impl/item.h
  void set_item(Bucket* located_bucket, size_t item_index, const ft_key_t key,
                const char* value);

  // fixedtable_impl/lock.h
  void lock_bucket(Bucket* bucket);
  void unlock_bucket(Bucket* bucket);
  void lock_extra_bucket_free_list();
  void unlock_extra_bucket_free_list();
  uint32_t read_version_begin(const Bucket* bucket) const;
  uint32_t read_version_end(const Bucket* bucket) const;

 public:
  std::string name;  // Name of the table

 private:
  ::mica::util::Config config_;
  size_t val_size;  // Size of each value
  erpc::HugeAlloc* alloc_;
  const ft_key_t ft_invalid_key;  // Invalid key to check for empty buckets

  Bucket* buckets_ = NULL;
  Bucket* extra_buckets_ = NULL;  // = (buckets + num_buckets); extra_buckets[0]
                                  // is not used because index 0 indicates "no
                                  // more extra bucket"

  uint8_t concurrent_access_mode_;

  uint32_t num_buckets_;
  uint32_t num_buckets_mask_;
  uint32_t num_extra_buckets_;

  // Padding to separate static and dynamic fields.
  char padding0[128];

  ExtraBucketFreeList extra_bucket_free_list_;

  mutable Stats stats_;
};
}  // namespace table
}  // namespace mica

#include "fixedtable_impl/bucket.h"
#include "fixedtable_impl/del.h"
#include "fixedtable_impl/get.h"
#include "fixedtable_impl/info.h"
#include "fixedtable_impl/init.h"
#include "fixedtable_impl/item.h"
#include "fixedtable_impl/lock.h"
#include "fixedtable_impl/prefetch.h"
#include "fixedtable_impl/set.h"

#endif
