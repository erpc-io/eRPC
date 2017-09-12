#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_LOCK_BKT_FOR_INS_
#define MICA_TABLE_FIXED_TABLE_IMPL_LOCK_BKT_FOR_INS_

namespace mica {
namespace table {

template <class StaticConfig>
Result FixedTable<StaticConfig>::lock_bkt_for_ins(uint32_t caller_id,
    uint64_t key_hash, ft_key_t key, uint64_t *out_timestamp) {
  assert(is_primary);

  uint32_t bucket_index = calc_bucket_index(key_hash);
  Bucket* bucket = get_bucket(bucket_index);

  bool lock_success = lock_bucket_ptr(caller_id, bucket);
  if(lock_success) {
    // We acquired the lock, or we were already holding it for @caller_id
    Bucket* located_bucket;
    size_t item_index = find_item_index(bucket, key, &located_bucket);

    if (item_index == StaticConfig::kBucketCap) {
      // The key does not exist
      *out_timestamp = bucket->timestamp;
      return Result::kSuccess;
    }

    // The key exists, i.e., we failed. unlock_bucket_ptr() will only release
    // the lock that was previously acquired by this function. Other locks
    // acquired by @caller_id on this bucket are still held.
    unlock_bucket_ptr(caller_id, bucket);
    return Result::kExists;
  } else {
    return Result::kLocked;
  }

}
}
}

#endif
