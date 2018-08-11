#pragma once

namespace mica {
namespace table {
template <class StaticConfig>
Result FixedTable<StaticConfig>::del(uint64_t key_hash, const ft_key_t& key) {
  uint32_t bucket_index = calc_bucket_index(key_hash);

  Bucket* bucket = get_bucket(bucket_index);

  lock_bucket(bucket);

  Bucket* located_bucket;
  size_t item_index = find_item_index(bucket, key, &located_bucket);
  if (item_index == StaticConfig::kBucketCap) {
    unlock_bucket(bucket);
    stat_inc(&Stats::delete_notfound);
    return Result::kNotFound;
  }

  located_bucket->key_arr[item_index] = ft_invalid_key;
  stat_dec(&Stats::count);

  fill_hole(located_bucket, item_index);

  unlock_bucket(bucket);

  stat_inc(&Stats::delete_found);
  return Result::kSuccess;
}
}
}
