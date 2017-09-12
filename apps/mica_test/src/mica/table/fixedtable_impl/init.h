#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_INIT_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_INIT_H_

namespace mica {
namespace table {
template <class StaticConfig>
FixedTable<StaticConfig>::FixedTable(const ::mica::util::Config& config,
     size_t val_size, int bkt_shm_key, Alloc* alloc, bool is_primary) :
     config_(config), val_size(val_size), bkt_shm_key(bkt_shm_key),
     alloc_(alloc), is_primary(is_primary) {
  assert(val_size % sizeof(uint64_t) == 0); // Make buckets 8-byte aligned

  // The Bucket struct does not contain values
  bkt_size_with_val = sizeof(Bucket) + (val_size * StaticConfig::kBucketCap);

  name = config.get("name").get_str();
  assert(bkt_shm_key > 0 && bkt_shm_key < 1024 * 1024);
  size_t item_count = config.get("item_count").get_uint64();
  // Compensate the load factor.
  item_count = item_count * 11 / 10;

  size_t num_buckets =
      (item_count + StaticConfig::kBucketCap - 1) / StaticConfig::kBucketCap;

  double extra_collision_avoidance = 0.1;
  size_t num_extra_buckets = static_cast<size_t>(
      static_cast<double>(num_buckets) * extra_collision_avoidance);

  size_t numa_node = config.get("numa_node").get_uint64();

  assert(num_buckets > 0);

  size_t log_num_buckets = 0;
  while (((size_t)1 << log_num_buckets) < num_buckets) log_num_buckets++;
  num_buckets = (size_t)1 << log_num_buckets;
  assert(log_num_buckets <= 32);

  num_buckets_ = ::mica::util::safe_cast<uint32_t>(num_buckets);
  num_buckets_mask_ = ::mica::util::safe_cast<uint32_t>(num_buckets - 1);
  num_extra_buckets_ = ::mica::util::safe_cast<uint32_t>(num_extra_buckets);

  {
    size_t shm_size =
        Alloc::roundup(bkt_size_with_val * (num_buckets_ + num_extra_buckets_));

    // TODO: Extend num_extra_buckets_ to meet shm_size.

    buckets_ = reinterpret_cast<Bucket*>(alloc->hrd_malloc_socket(bkt_shm_key,
        shm_size, numa_node));	// Zeroes out per-bucket timestamps
    assert(buckets_ != NULL);
  }

  // subtract by one to compensate 1-base indices
  extra_buckets_ = reinterpret_cast<Bucket*>((uint8_t *) buckets_ +
                   ((num_buckets_ - 1) * bkt_size_with_val));
  // the rest extra_bucket information is initialized in reset()

  reset();

  if (StaticConfig::kVerbose) {
    fprintf(stderr, "warning: kVerbose is defined (low performance)\n");

    if (StaticConfig::kCollectStats)
      fprintf(stderr, "warning: kCollectStats is defined (low performance)\n");

    fprintf(stderr, "info: num_buckets = %u\n", num_buckets_);
    fprintf(stderr, "info: num_extra_buckets = %u\n", num_extra_buckets_);

    fprintf(stderr, "\n");
  }
}

template <class StaticConfig>
FixedTable<StaticConfig>::~FixedTable() {
  printf("Destroying table %s\n", name.c_str());
  reset();

  //if (!alloc_->unmap(buckets_)) assert(false);
  if(!alloc_->hrd_free(bkt_shm_key, buckets_)) assert(false);
}

template <class StaticConfig>
void FixedTable<StaticConfig>::reset() {

  // Initialize bucket metadata + fill in invalid keys
  for (size_t bkt_i = 0; bkt_i < num_buckets_ + num_extra_buckets_; bkt_i++) {
    Bucket *bucket = get_bucket(bkt_i);
    bucket->timestamp = (kTimestampCanary << 61);
    bucket->locker_id = kInvalidCallerId;
    bucket->num_locks = 0;

    for (size_t item_index = 0; item_index < StaticConfig::kBucketCap;
        item_index++) {
      bucket->key_arr[item_index] = kFtInvalidKey;
    }
  }

  // initialize a free list of extra buckets
  extra_bucket_free_list_.lock = 0;

  if (num_extra_buckets_ == 0)
    extra_bucket_free_list_.head = 0;  // no extra bucket at all
  else {
    uint32_t extra_bucket_index;
    for (extra_bucket_index = 1;
         extra_bucket_index < 1 + num_extra_buckets_ - 1; extra_bucket_index++) {
      get_extra_bucket(extra_bucket_index)->next_extra_bucket_index =
          extra_bucket_index + 1;
    }

    get_extra_bucket(extra_bucket_index)->next_extra_bucket_index =
        0;  // no more free extra bucket

    extra_bucket_free_list_.head = 1;
  }

  reset_stats(true);
}
}
}

#endif
