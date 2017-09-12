#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_DEL_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_DEL_H_

namespace mica {
namespace table {
template <class StaticConfig>
Result FixedTable<StaticConfig>::del(uint32_t caller_id, uint64_t key_hash,
                                     ft_key_t key) {
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

  // The key must exist - we checked this when we acquired the bucket lock
  // at the primary
  assert(item_index < StaticConfig::kBucketCap);

  // If we are here, the key exists
  located_bucket->key_arr[item_index] = kFtInvalidKey;
  stat_dec(&Stats::count);

  fill_hole(located_bucket, item_index);

  // Coordinators acquire bucket locks at primary. No need to unlock at backups.
  if(is_primary) {
    // unlock_bucket_ptr() will only release the lock that was previously
    // acquired at the primary for this key's deletion. Other locks acquired by
    // @caller_id on this bucket are still held.
    unlock_bucket_ptr(caller_id, bucket);
  }

  stat_inc(&Stats::delete_found);
  return Result::kSuccess;
}
}
}

#endif
