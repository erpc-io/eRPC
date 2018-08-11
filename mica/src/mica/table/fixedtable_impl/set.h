#pragma once

namespace mica {
namespace table {
template <class StaticConfig>
Result FixedTable<StaticConfig>::set(uint64_t key_hash, const ft_key_t& key,
                                     const char* value) {
  if (key == ft_invalid_key) return Result::kRejected;

  uint32_t bucket_index = calc_bucket_index(key_hash);

  Bucket* bucket = get_bucket(bucket_index);

  lock_bucket(bucket);

  Bucket* located_bucket;
  size_t item_index = find_item_index(bucket, key, &located_bucket);

  if (item_index == StaticConfig::kBucketCap) {
    item_index = get_empty(bucket, &located_bucket);
    if (item_index == StaticConfig::kBucketCap) {
      // no more space
      // TODO: add a statistics entry
      unlock_bucket(bucket);
      return Result::kInsufficientSpaceIndex;
    }

    stat_inc(&Stats::set_new);
  }

  set_item(located_bucket, item_index, key, value);

  unlock_bucket(bucket);
  return Result::kSuccess;
}
}
}
