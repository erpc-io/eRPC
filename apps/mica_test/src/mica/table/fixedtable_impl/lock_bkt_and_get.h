#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_LOCK_BKT_AND_GET_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_LOCK_BKT_AND_GET_H_

namespace mica {
namespace table {

template <class StaticConfig>
Result FixedTable<StaticConfig>::lock_bkt_and_get(uint32_t caller_id,
    uint64_t key_hash, ft_key_t key, uint64_t *out_timestamp, char *value) {
  assert(is_primary);

  uint32_t bucket_index = calc_bucket_index(key_hash);
  Bucket* bucket = get_bucket(bucket_index);

  bool lock_success = lock_bucket_ptr(caller_id, bucket);
  if(lock_success) {
    // We acquired the lock, or we were already holding it for @caller_id
    Bucket* located_bucket;
    size_t item_index = find_item_index(bucket, key, &located_bucket);

    if (item_index < StaticConfig::kBucketCap) {
      // The key exists
      *out_timestamp = bucket->timestamp;
      uint64_t *_val = (uint64_t *) get_value(located_bucket, item_index);
      ::mica::util::memcpy(value, _val, val_size);
      return Result::kSuccess;
    }

    // The key does not exist, i.e., we failed. unlock_bucket_ptr() will only
    // release the lock that this function acquired. Other locks acquired
    // by @caller_id on this bucket are still held.
    unlock_bucket_ptr(caller_id, bucket);
    return Result::kNotFound;
  } else {
    return Result::kLocked;
  }

}
}
}

#endif
