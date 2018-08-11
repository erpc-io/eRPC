#pragma once

namespace mica {
namespace table {

template <class StaticConfig>
void FixedTable<StaticConfig>::set_item(Bucket *located_bucket,
                                        size_t item_index, ft_key_t key,
                                        const char *value) {
  located_bucket->key_arr[item_index] = key;
  uint8_t *_val = get_value(located_bucket, item_index);
  ::mica::util::memcpy(_val, value, val_size);
}
}
}
