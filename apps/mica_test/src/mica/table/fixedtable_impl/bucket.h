#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_BUCKET_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_BUCKET_H_

namespace mica {
namespace table {

template <class StaticConfig>
uint32_t FixedTable<StaticConfig>::calc_bucket_index(uint64_t key_hash) const {
  return static_cast<uint32_t>(key_hash) & num_buckets_mask_;
}

// Get the value at index @item_index in this bucket
template <class StaticConfig>
uint8_t* FixedTable<StaticConfig>::get_value(
    const Bucket *bucket, size_t item_index) const {
  return (uint8_t *) bucket + sizeof(Bucket) + (item_index * val_size);
}

template <class StaticConfig>
typename FixedTable<StaticConfig>::Bucket*
FixedTable<StaticConfig>::get_bucket(uint32_t bucket_index) {
  assert(buckets_ != NULL);
  assert(bucket_index < num_buckets_ + num_extra_buckets_);
  return reinterpret_cast<Bucket*>((uint8_t *) buckets_ +
                                   (bucket_index * bkt_size_with_val));
}

template <class StaticConfig>
const typename FixedTable<StaticConfig>::Bucket*
FixedTable<StaticConfig>::get_bucket(uint32_t bucket_index) const {
  assert(buckets_ != NULL);
  assert(bucket_index < num_buckets_ + num_extra_buckets_);
  return reinterpret_cast<Bucket*>((uint8_t *) buckets_ +
                                   (bucket_index * bkt_size_with_val));
}

template <class StaticConfig>
bool FixedTable<StaticConfig>::has_extra_bucket(const Bucket* bucket) {
  return bucket->next_extra_bucket_index != 0;
}

template <class StaticConfig>
const typename FixedTable<StaticConfig>::Bucket*
FixedTable<StaticConfig>::get_extra_bucket(uint32_t extra_bucket_index) const {
  // extra_bucket_index is 1-base
  assert(extra_bucket_index != 0);
  assert(extra_bucket_index < 1 + num_extra_buckets_);

  // extra_buckets[1] is the actual start
  return reinterpret_cast<Bucket*>((uint8_t *) extra_buckets_ +
                                  (extra_bucket_index * bkt_size_with_val));
}

template <class StaticConfig>
typename FixedTable<StaticConfig>::Bucket*
FixedTable<StaticConfig>::get_extra_bucket(uint32_t extra_bucket_index) {
  assert(extra_bucket_index != 0);
  assert(extra_bucket_index < 1 + num_extra_buckets_);

  // extra_buckets[1] is the actual start
  return reinterpret_cast<Bucket*>((uint8_t *) extra_buckets_ +
                                  (extra_bucket_index * bkt_size_with_val));
}

template <class StaticConfig>
bool FixedTable<StaticConfig>::alloc_extra_bucket(Bucket* bucket) {
  assert(!has_extra_bucket(bucket));

  lock_extra_bucket_free_list();

  if (extra_bucket_free_list_.head == 0) {
    unlock_extra_bucket_free_list();
    return false;
  }

  // take the first free extra bucket
  uint32_t extra_bucket_index = extra_bucket_free_list_.head;
  extra_bucket_free_list_.head =
      get_extra_bucket(extra_bucket_index)->next_extra_bucket_index;
  get_extra_bucket(extra_bucket_index)->next_extra_bucket_index = 0;

  // add it to the given bucket
  // concurrent readers may see the new extra_bucket from this point
  bucket->next_extra_bucket_index = extra_bucket_index;

  unlock_extra_bucket_free_list();
  return true;
}

template <class StaticConfig>
void FixedTable<StaticConfig>::free_extra_bucket(Bucket* bucket) {
  assert(has_extra_bucket(bucket));

  uint32_t extra_bucket_index = bucket->next_extra_bucket_index;

  Bucket* extra_bucket = get_extra_bucket(extra_bucket_index);
  assert(extra_bucket->next_extra_bucket_index ==
         0);  // only allows freeing the tail of the extra bucket chain

  // verify if the extra bucket is empty (debug only)
  for (size_t item_index = 0; item_index < StaticConfig::kBucketCap;
       item_index++)
    assert(extra_bucket->key_arr[item_index] == kFtInvalidKey);

  // detach
  bucket->next_extra_bucket_index = 0;

  // add freed extra bucket to the free list
  lock_extra_bucket_free_list();

  extra_bucket->next_extra_bucket_index = extra_bucket_free_list_.head;
  extra_bucket_free_list_.head = extra_bucket_index;

  unlock_extra_bucket_free_list();
}

template <class StaticConfig>
void FixedTable<StaticConfig>::fill_hole(Bucket* bucket, size_t unused_item_index) {
  while (true) {
    // there is no extra bucket; do not try to fill a hole within the same
    // bucket
    if (!has_extra_bucket(bucket)) return;

    Bucket* prev_extra_bucket = nullptr;
    Bucket* current_extra_bucket = bucket;
    do {
      prev_extra_bucket = current_extra_bucket;
      current_extra_bucket =
          get_extra_bucket(current_extra_bucket->next_extra_bucket_index);
    } while (has_extra_bucket(current_extra_bucket) != 0);

    bool last_item = true;
    size_t moved_item_index = StaticConfig::kBucketCap;

    for (size_t item_index = 0; item_index < StaticConfig::kBucketCap;
         item_index++)
      if (current_extra_bucket->key_arr[item_index] != kFtInvalidKey) {
        if (moved_item_index == StaticConfig::kBucketCap)
          moved_item_index = item_index;
        else {
          last_item = false;
          break;
        }
      }

    if (moved_item_index == StaticConfig::kBucketCap) {
      // The last extra bucket in the chain was already empty.  This happens if
      // one of the previous delete operation has removed the last item from the
      // last bucket; fill_hole() does not remove such an empty extra bucket
      // because it does not know the previous bucket (which could be not even
      // an extra bucket) in the chain.  We release this extra bucket from the
      // chain and restart.
      free_extra_bucket(prev_extra_bucket);
      continue;
    }

    // move the entry
    bucket->key_arr[unused_item_index] =
      current_extra_bucket->key_arr[moved_item_index];

    uint8_t *_unused_item_val = get_value(bucket, unused_item_index);
    uint8_t *_moved_item_val = get_value(current_extra_bucket, moved_item_index);
    ::mica::util::memcpy<8>(_unused_item_val, _moved_item_val, val_size);

    current_extra_bucket->key_arr[moved_item_index] = kFtInvalidKey;

    // if it was the last entry of current_extra_bucket, free
    // current_extra_bucket
    if (last_item) free_extra_bucket(prev_extra_bucket);
    break;
  }
}

template <class StaticConfig>
size_t FixedTable<StaticConfig>::get_empty(Bucket* bucket,
                                       Bucket** located_bucket) {
  Bucket* current_bucket = bucket;
  while (true) {
    for (size_t item_index = 0; item_index < StaticConfig::kBucketCap;
         item_index++) {
      if (current_bucket->key_arr[item_index] == kFtInvalidKey) {
        *located_bucket = current_bucket;
        return item_index;
      }
    }
    if (!has_extra_bucket(current_bucket)) break;
    current_bucket = get_extra_bucket(current_bucket->next_extra_bucket_index);
  }

  // no space; alloc new extra_bucket
  if (alloc_extra_bucket(current_bucket)) {
    *located_bucket = get_extra_bucket(current_bucket->next_extra_bucket_index);
    return 0;  // use the first slot (it should be empty)
  } else {
    // no free extra_bucket
    *located_bucket = nullptr;
    return StaticConfig::kBucketCap;
  }
}

template <class StaticConfig>
size_t FixedTable<StaticConfig>::find_item_index(
    const Bucket* bucket, ft_key_t key, const Bucket** located_bucket) const {
  const Bucket* current_bucket = bucket;

  while (true) {
    for (size_t item_index = 0; item_index < StaticConfig::kBucketCap;
         item_index++) {
      // we may read garbage values, which do not cause any fatal issue
      if (current_bucket->key_arr[item_index] != key) continue;

      // we skip any validity check because it will be done by callers who are
      // doing
      // more jobs with this result

      if (StaticConfig::kVerbose) printf("find item index: %zu\n", item_index);
      *located_bucket = current_bucket;
      return item_index;
    }

    if (!has_extra_bucket(current_bucket)) break;
    current_bucket = get_extra_bucket(current_bucket->next_extra_bucket_index);
  }

  if (StaticConfig::kVerbose) printf("could not find item index\n");

  *located_bucket = nullptr;
  return StaticConfig::kBucketCap;
}

template <class StaticConfig>
size_t FixedTable<StaticConfig>::find_item_index(Bucket* bucket, ft_key_t key,
                                                 Bucket** located_bucket) {
  return find_item_index(const_cast<const Bucket*>(bucket), key,
                         const_cast<const Bucket**>(located_bucket));
}

template <class StaticConfig>
double FixedTable<StaticConfig>::get_locked_bkt_fraction() {
  size_t num_locked_buckets = 0;
  // Examine only primary buckets
  for (size_t bkt_i = 0; bkt_i < num_buckets_; bkt_i++) {
    Bucket *bucket = get_bucket(bkt_i);
    if(is_locked(bucket->timestamp)) {
      num_locked_buckets++;
    }
  }

  return (double) num_locked_buckets / num_buckets_;
}

template <class StaticConfig>
void FixedTable<StaticConfig>::print_bucket_occupancy() {
  size_t primary_slots_used = 0, extra_slots_used = 0;
  // A bucket is considered used if any of its slots is used
  size_t primary_bkts_used = 0, extra_bkts_used = 0;

  for (size_t bkt_i = 0; bkt_i < num_buckets_ + num_extra_buckets_; bkt_i++) {
    Bucket *bucket = get_bucket(bkt_i);
    bool bkt_used = false;
    for (size_t item_index = 0; item_index < StaticConfig::kBucketCap;
        item_index++) {
      if(bucket->key_arr[item_index] != kFtInvalidKey) {
        bkt_i < num_buckets_ ? primary_slots_used++ : extra_slots_used++;
        bkt_used = true;
      }
    }

    if (bkt_used) {
      bkt_i < num_buckets_ ? primary_bkts_used++ : extra_bkts_used++;
    }
  }

  // Primary buckets
  printf("Table %s: Primary bucket occupancy = %.4f (%zu of %u buckets used)\n",
    name.c_str(),
    (double) primary_bkts_used / num_buckets_, primary_bkts_used, num_buckets_);

  printf("Table %s: Primary slot occupancy = %.4f (%zu of %zu slots used)\n",
    name.c_str(),
    (double) primary_slots_used / (num_buckets_ * StaticConfig::kBucketCap),
    primary_slots_used, num_buckets_ * StaticConfig::kBucketCap);

  // Secondary buckets
  printf("Table %s: Extra bucket occupancy = %.4f (%zu of %u buckets used)\n",
    name.c_str(),
    (double) extra_bkts_used / num_extra_buckets_, extra_bkts_used,
    num_extra_buckets_);

  printf("Table %s: Extra slot occupancy = %.4f (%zu of %zu slots used)\n",
    name.c_str(),
    (double) extra_slots_used / (num_extra_buckets_ * StaticConfig::kBucketCap),
    extra_slots_used, num_extra_buckets_ * StaticConfig::kBucketCap);

  printf("Table %s: Memory efficiency = %.2f\n", name.c_str(),
    (double) (primary_slots_used + extra_slots_used) /
        ((num_buckets_ + num_extra_buckets_) * StaticConfig::kBucketCap));
}
}
}

#endif
