#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_GET_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_GET_H_

namespace mica {
namespace table {
template <class StaticConfig>
/**
 * @param key_hash The hash of the key computed using mica::util::hash
 * @param key The key to get()
 * @param out_value Pointer to a buffer to copy the value to. The buffer should
 * have space for StaticConfig::kValSize bytes
 */
Result FixedTable<StaticConfig>::get(uint32_t caller_id, uint64_t key_hash,
                                     ft_key_t key, uint64_t *out_timestamp,
                                     char* out_value) const {
  assert(is_primary);

  uint32_t bucket_index = calc_bucket_index(key_hash);
  const Bucket* bucket = get_bucket(bucket_index);

  uint64_t timestamp_start = read_timestamp(bucket);

  if(is_unlocked(timestamp_start)) {
    // Bucket is unlocked - try to read the value optimistically
    const Bucket* located_bucket;
    size_t item_index = find_item_index(bucket, key, &located_bucket);

    if (item_index == StaticConfig::kBucketCap) {
      // Key does not exist. We still need to set the timestamp.
      *out_timestamp = timestamp_start;

      if (timestamp_start != read_timestamp(bucket)) {
        // The bucket got locked by some other caller ID (it could not have been
        // this @caller_id. In this case, the timestamp copied to @out_timestamp
        // must not be used.
        stat_inc(&Stats::get_locked);
        return Result::kLocked;
      }

      stat_inc(&Stats::get_notfound);
      return Result::kNotFound;
    }

    *out_timestamp = timestamp_start;
    uint8_t *_val = get_value(located_bucket, item_index);
    ::mica::util::memcpy(out_value, _val, val_size);

    if (timestamp_start != read_timestamp(bucket)) {
      stat_inc(&Stats::get_locked);
      return Result::kLocked;
    }

    stat_inc(&Stats::get_found);
    return Result::kSuccess;
  } else if(bucket->locker_id == caller_id) {
    // Since all operations by @caller_id on this machine will be processed by
    // this thread, this means that @caller_id holds the bucket lock, and
    // the bucket won't get unlocked while this function executes.
    const Bucket* located_bucket;
    size_t item_index = find_item_index(bucket, key, &located_bucket);

    // This thread holds the bucket lock, so the timestamp cannot have changed
    // since we first read it.
    assert(timestamp_start == read_timestamp(bucket));

    if (item_index == StaticConfig::kBucketCap) {
      stat_inc(&Stats::get_notfound);
      return Result::kNotFound;
    }

    *out_timestamp = timestamp_start;
    uint8_t *_val = get_value(located_bucket, item_index);
    ::mica::util::memcpy(out_value, _val, val_size);

    stat_inc(&Stats::get_found);
    return Result::kSuccess;
  } else {
    // Some other caller held the lock when we checked
    stat_inc(&Stats::get_locked);
    return Result::kLocked;
  }
}
}
}

#endif
