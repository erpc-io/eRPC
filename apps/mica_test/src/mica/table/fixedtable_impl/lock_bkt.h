#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_LOCK_BKT_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_LOCK_BKT_H_

// This is a datapath API function exposed to users. The internal locking
// function @lock_bucket_ptr() is not exposed to users.
namespace mica {
namespace table {
template <class StaticConfig>
Result FixedTable<StaticConfig>::lock_bucket_hash(uint32_t caller_id,
                                                  uint64_t key_hash) {
  assert(is_primary);

  uint32_t bucket_index = calc_bucket_index(key_hash);
  Bucket* bucket = get_bucket(bucket_index);

  bool res = lock_bucket_ptr(caller_id, bucket);

  return res ? Result::kSuccess : Result::kLocked;
}
}
}

#endif
