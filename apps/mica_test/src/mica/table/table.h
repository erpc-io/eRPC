#pragma once
#ifndef MICA_TABLE_TABLE_H_
#define MICA_TABLE_TABLE_H_

#include "mica/common.h"
#include "mica/table/types.h"

namespace mica {
namespace table {
class TableInterface {
 public:
  void reset();

  Result del(uint64_t key_hash, const char* key, size_t key_length);

  Result get(uint64_t key_hash, const char* key, size_t key_length,
             char* out_value, size_t in_value_length, size_t* out_value_length,
             bool allow_mutation) const;

  Result increment(uint64_t key_hash, const char* key, size_t key_length,
                   uint64_t increment, uint64_t* out_value);

  Result set(uint64_t key_hash, const char* key, size_t key_length,
             const char* value, size_t value_length, bool overwrite);

  Result test(uint64_t key_hash, const char* key, size_t key_length) const;

  void prefetch_table(uint64_t key_hash) const;
  void prefetch_pool(uint64_t key_hash) const;

  void print_buckets() const;
  void print_stats() const;
  void reset_stats(bool reset_count);
};
}
}

#endif
