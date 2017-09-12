#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_LOCK_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_LOCK_H_

namespace mica {
namespace table {

template <class StaticConfig>
uint64_t FixedTable<StaticConfig>::read_timestamp(const Bucket* bucket) const {
  uint64_t ts = *(volatile uint64_t*)&bucket->timestamp;
  ::mica::util::memory_barrier();
  return ts;
}

template <class StaticConfig>
bool FixedTable<StaticConfig>::is_unlocked(uint64_t timestamp) const {
  return ((timestamp & 1ull) == 0ull);
}

template <class StaticConfig>
bool FixedTable<StaticConfig>::is_locked(uint64_t timestamp) const {
  return ((timestamp & 1ull) == 1ull);
}

template <class StaticConfig>
bool FixedTable<StaticConfig>::lock_bucket_ptr(uint32_t caller_id,
                                               Bucket* bucket) {
  assert(caller_id != kInvalidCallerId);

  // Optimistically assume that the timestamp is even and try to make it odd
  uint64_t ts = *(volatile uint64_t*)&bucket->timestamp & ~1ull;
  uint64_t new_ts = ts | 1ull;
  if (__sync_bool_compare_and_swap((volatile uint64_t*)&bucket->timestamp,
                                   ts, new_ts)) {
    assert(bucket->locker_id == kInvalidCallerId);
    assert(bucket->num_locks == 0);

    bucket->locker_id = caller_id;  // Record the locker ID
    bucket->num_locks = 1;
    return true;
  }

  // Locking failed. Check if the lock is held by this caller.
  if(bucket->locker_id == caller_id) {
    // @locker_id is invalidated before unlocking, so we're sure that @caller_id
    // holds the lock. As all reqs from @caller_id at this machine are handled
    // by this thread, we're sure that the bucket won't get unlocked while
    // this function executes.
    assert(is_locked(bucket->timestamp)); // Must be already locked
    assert(bucket->num_locks > 0); // We've already locked this bucket once
    assert(bucket->num_locks < kMaxNumLocks);

    bucket->num_locks++;
    return true;
  }
  
  return false; // Some other caller holds the lock
}

template <class StaticConfig>
void FixedTable<StaticConfig>::unlock_bucket_ptr(uint32_t caller_id,
                                                 Bucket* bucket) {
  assert(caller_id != kInvalidCallerId);

  // All reqs from @caller_id at this machine are handled by this thread, and
  // @caller_id holds the bucket lock, so this is race-free

  assert(is_locked(bucket->timestamp));
  assert(bucket->locker_id == caller_id);
  assert(bucket->num_locks > 0);

  if(bucket->num_locks == 1) {
    bucket->locker_id = kInvalidCallerId;
    bucket->num_locks = 0;

    // no need to use atomic add
    ::mica::util::memory_barrier();
    (*(volatile uint64_t*)&bucket->timestamp)++;
  } else {
    // @bucket remains locked after this decrement. There will eventually be
    // a barrier when we unlock for the last lock, so no need for barrier here.
    bucket->num_locks--;
  }
}

template <class StaticConfig>
void FixedTable<StaticConfig>::lock_extra_bucket_free_list() {
  while (true) {
    if (__sync_bool_compare_and_swap(
        (volatile uint8_t*)&extra_bucket_free_list_.lock, 0U, 1U))
        break;
  }
}

template <class StaticConfig>
void FixedTable<StaticConfig>::unlock_extra_bucket_free_list() {
  ::mica::util::memory_barrier();
  assert((*(volatile uint8_t*)&extra_bucket_free_list_.lock & 1U) == 1U);
  // no need to use atomic add
  *(volatile uint8_t*)&extra_bucket_free_list_.lock = 0U;
}


}
}

#endif
