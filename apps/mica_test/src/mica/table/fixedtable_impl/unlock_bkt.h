#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_UNLOCK_BKT_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_UNLOCK_BKT_H_

namespace mica {
namespace table {
template <class StaticConfig>
// This is a datapath API function exposed to users. The internal locking
// function @lock_bucket_ptr() is not exposed to users.
Result FixedTable<StaticConfig>::unlock_bucket_hash(uint32_t caller_id,
                                                    uint64_t key_hash) {
  assert(is_primary);

  uint32_t bucket_index = calc_bucket_index(key_hash);
  Bucket* bucket = get_bucket(bucket_index);

  // unlock_bucket_ptr() will only release one lock if @caller_id holds multiple
  // locks on this bucket. It will also do sanity checks.
  unlock_bucket_ptr(caller_id, bucket);
  return Result::kSuccess;
}
}
}

#endif
