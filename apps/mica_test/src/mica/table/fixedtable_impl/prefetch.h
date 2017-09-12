#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_PREFETCH_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_PREFETCH_H_

namespace mica {
namespace table {
template <class StaticConfig>
void FixedTable<StaticConfig>::prefetch_table(uint64_t key_hash) const {
  uint32_t bucket_index = calc_bucket_index(key_hash);
  const Bucket* bucket = get_bucket(bucket_index);

  // bucket address is already 64-byte aligned

  // When value size is 16B, we need to prefetch 3 cache lines. For larger
  // values, we may need to prefetch more cache lines, but it does not seem to
  // improve performance.
  __builtin_prefetch(bucket, 0, 0);
  __builtin_prefetch(reinterpret_cast<const char*>(bucket) + 64, 0, 0);
  __builtin_prefetch(reinterpret_cast<const char*>(bucket) + 128, 0, 0);

}

}
}

#endif
