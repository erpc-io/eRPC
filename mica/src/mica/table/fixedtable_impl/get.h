#pragma once

namespace mica {
namespace table {
template <class StaticConfig>
/**
 * @param key_hash The hash of the key computed using mica::util::hash
 * @param key The key to get()
 * @param out_value Pointer to a buffer to copy the value to. The buffer should
 * have space for StaticConfig::kValSize bytes
 */
Result FixedTable<StaticConfig>::get(uint64_t key_hash, const ft_key_t& key,
                                     char* out_value) const {
  uint32_t bucket_index = calc_bucket_index(key_hash);
  const Bucket* bucket = get_bucket(bucket_index);

  while (true) {
    uint32_t version_start = read_version_begin(bucket);

    const Bucket* located_bucket;
    size_t item_index = find_item_index(bucket, key, &located_bucket);
    if (item_index == StaticConfig::kBucketCap) {
      if (version_start != read_version_end(bucket)) continue; /* Try again */
      stat_inc(&Stats::get_notfound);
      return Result::kNotFound;
    }

    uint8_t* _val = get_value(located_bucket, item_index);
    ::mica::util::memcpy<8>(out_value, _val, val_size);

    if (version_start != read_version_end(bucket)) continue; /* Try again */

    stat_inc(&Stats::get_found);
    break;
  }

  return Result::kSuccess;
}
}
}
