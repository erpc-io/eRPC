#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_DEBUG_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_DEBUG_H_

namespace mica {
namespace table {
template <class StaticConfig>
void FixedTable<StaticConfig>::print_bucket(const Bucket* bucket) const {
  printf("<bucket %zx>: ", (size_t)bucket);
  for (size_t item_index = 0; item_index < StaticConfig::kBucketCap;
       item_index++) {
    printf("key %zx, ", static_cast<size_t>(bucket->item_vec[item_index].key));
  }

  printf("\n");
}

template <class StaticConfig>
void FixedTable<StaticConfig>::print_buckets() const {
  for (size_t bucket_index = 0; bucket_index < num_buckets_; bucket_index++) {
    Bucket* bucket =  get_bucket(bucket_index);
    print_bucket(bucket);
  }
  printf("\n");
}

template <class StaticConfig>
void FixedTable<StaticConfig>::print_stats() const {
  if (StaticConfig::kCollectStats) {
    printf("count:                  %10zu\n", stats_.count);
    printf("set_new:                %10zu | ", stats_.set_new);
    printf("get_found:              %10zu | ", stats_.get_found);
    printf("get_notfound:           %10zu\n", stats_.get_notfound);
    printf("test_found:             %10zu | ", stats_.test_found);
    printf("test_notfound:          %10zu\n", stats_.test_notfound);
    printf("delete_found:           %10zu | ", stats_.delete_found);
    printf("delete_notfound:        %10zu\n", stats_.delete_notfound);
  }
}

template <class StaticConfig>
void FixedTable<StaticConfig>::reset_stats(bool reset_count) {
  if (StaticConfig::kCollectStats) {
    size_t count = stats_.count;
    ::mica::util::memset(&stats_, 0, sizeof(stats_));
    if (!reset_count) stats_.count = count;
  }
}

template <class StaticConfig>
void FixedTable<StaticConfig>::stat_inc(size_t Stats::*counter) const {
  if (StaticConfig::kCollectStats) __sync_add_and_fetch(&(stats_.*counter), 1);
}

template <class StaticConfig>
void FixedTable<StaticConfig>::stat_dec(size_t Stats::*counter) const {
  if (StaticConfig::kCollectStats) __sync_sub_and_fetch(&(stats_.*counter), 1);
}
}
}

#endif
