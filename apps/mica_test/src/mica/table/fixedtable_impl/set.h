#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_SET_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_SET_H_

namespace mica {
namespace table {
template <class StaticConfig>
Result FixedTable<StaticConfig>::set(uint32_t caller_id, uint64_t key_hash,
                                     ft_key_t key, const char* value) {
  // Can be called at both primary and backup datastores
  uint32_t bucket_index = calc_bucket_index(key_hash);
  Bucket* bucket = get_bucket(bucket_index);

  // We must be holding the lock on this bucket at primaries.
  if(is_primary) {
    assert(is_locked(bucket->timestamp));
    assert(bucket->locker_id == caller_id);
  }

  Bucket* located_bucket;
  size_t item_index = find_item_index(bucket, key, &located_bucket);

  if (item_index == StaticConfig::kBucketCap) {
    // The key does not exist in the table
    item_index = get_empty(bucket, &located_bucket);
    if (item_index == StaticConfig::kBucketCap) {
      // No more space. This should be fatal.
      unlock_bucket_ptr(caller_id, bucket);
      return Result::kInsufficientSpaceIndex;	// This should be fatal
    }

    stat_inc(&Stats::set_new);
  }

  // Here, @located_bucket either contains @key at index @item_index, or is
  // empty at this slot.
  set_item(located_bucket, item_index, key, value);

  // Coordinators acquire bucket locks at primary. No need to unlock at backups.
  if(is_primary) {
    // unlock_bucket_ptr() will only release the lock that was previously
    // acquired at the primary for this set(). Other locks acquired by
    // @caller_id on this bucket are still held.
    unlock_bucket_ptr(caller_id, bucket);
  }

  return Result::kSuccess;
}
}
}

#endif
