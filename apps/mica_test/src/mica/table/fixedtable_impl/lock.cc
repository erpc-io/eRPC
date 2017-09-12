#pragma once
#ifndef MICA_TABLE_FIXED_TABLE_IMPL_LOCK_H_
#define MICA_TABLE_FIXED_TABLE_IMPL_LOCK_H_

#include "mica/table/fixedtable.h"

namespace mica {
namespace table {

template <class StaticConfig>
void FixedTable<StaticConfig>::lock_bucket(Bucket* bucket) {
  if (StaticConfig::kConcurrent) {
    if (concurrent_access_mode_ == 1) {
      assert((*(volatile uint32_t*)&bucket->version & 1U) == 0U);
      (*(volatile uint32_t*)&bucket->version)++;
      ::mica::util::memory_barrier();
    } else if (concurrent_access_mode_ == 2) {
      while (true) {
        uint32_t v = *(volatile uint32_t*)&bucket->version & ~1U;
        uint32_t new_v = v | 1U;
        if (__sync_bool_compare_and_swap((volatile uint32_t*)&bucket->version,
                                         v, new_v))
          break;
      }
    }
  }
}

template <class StaticConfig>
void FixedTable<StaticConfig>::unlock_bucket(Bucket* bucket) {
  if (StaticConfig::kConcurrent) {
    if (concurrent_access_mode_ != 0) {
      ::mica::util::memory_barrier();
      assert((*(volatile uint32_t*)&bucket->version & 1U) == 1U);
      // no need to use atomic add because this thread is the only one writing
      // to version
      (*(volatile uint32_t*)&bucket->version)++;
    }
  }
}

template <class StaticConfig>
void FixedTable<StaticConfig>::lock_extra_bucket_free_list() {
  if (StaticConfig::kConcurrent) {
    if (concurrent_access_mode_ == 2) {
      while (true) {
        if (__sync_bool_compare_and_swap(
                (volatile uint8_t*)&extra_bucket_free_list_.lock, 0U, 1U))
          break;
      }
    }
  }
}

template <class StaticConfig>
void FixedTable<StaticConfig>::unlock_extra_bucket_free_list() {
  if (StaticConfig::kConcurrent) {
    if (concurrent_access_mode_ == 2) {
      ::mica::util::memory_barrier();
      assert((*(volatile uint8_t*)&extra_bucket_free_list_.lock & 1U) == 1U);
      // no need to use atomic add because this thread is the only one writing
      // to version
      *(volatile uint8_t*)&extra_bucket_free_list_.lock = 0U;
    }
  }
}

template <class StaticConfig>
uint32_t FixedTable<StaticConfig>::read_version_begin(const Bucket* bucket) const {
  if (StaticConfig::kConcurrent) {
    if (concurrent_access_mode_ != 0) {
      while (true) {
        uint32_t v = *(volatile uint32_t*)&bucket->version;
        ::mica::util::memory_barrier();
        if ((v & 1U) != 0U) continue;
        return v;
      }
    } else
      return 0U;
  } else {
    return 0U;
  }
}

template <class StaticConfig>
uint32_t FixedTable<StaticConfig>::read_version_end(const Bucket* bucket) const {
  if (StaticConfig::kConcurrent) {
    if (concurrent_access_mode_ != 0) {
      ::mica::util::memory_barrier();
      uint32_t v = *(volatile uint32_t*)&bucket->version;
      return v;
    } else
      return 0U;
  } else {
    return 0U;
  }
}
}
}

#endif
