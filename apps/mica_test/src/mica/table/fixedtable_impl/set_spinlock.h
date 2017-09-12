#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_SET_LOCKED_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_SET_LOCKED_H_

namespace mica {
namespace table {
template <class StaticConfig>
Result FixedTable<StaticConfig>::set_spinlock(uint32_t caller_id,
    uint64_t key_hash, ft_key_t key, const char* value) {
  // Can be called *locally* at both primary and backup datastores
  uint32_t bucket_index = calc_bucket_index(key_hash);
  Bucket* bucket = get_bucket(bucket_index);

  while(!lock_bucket_ptr(caller_id, bucket)) {
    // Spin on the bucket lock
  }

  Bucket* located_bucket;
  size_t item_index = find_item_index(bucket, key, &located_bucket);

  if (item_index == StaticConfig::kBucketCap) {
    // The key does not exist in the table
    item_index = get_empty(bucket, &located_bucket);
    if (item_index == StaticConfig::kBucketCap) {
      // no more space
      unlock_bucket_ptr(caller_id, bucket);
      return Result::kInsufficientSpaceIndex;	// This should be fatal
    }

    stat_inc(&Stats::set_new);
  }

  // Here, @located_bucket either contains @key at index @item_index, or is
  // empty at this slot.
  set_item(located_bucket, item_index, key, value);
  unlock_bucket_ptr(caller_id, bucket);
  return Result::kSuccess;
}
}
}

#endif
