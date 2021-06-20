// Credits: Huanchen Zhang, Ziqi Wang

#ifndef MT_INDEX_API_H
#define MT_INDEX_API_H

// Masstree is a system directory
#include <json.hh>
#include <kvrow.hh>
#include <kvthread.hh>
#include <masstree.hh>
#include <masstree_insert.hh>
#include <masstree_remove.hh>
#include <masstree_scan.hh>
#include <masstree_tcursor.hh>
#include <query_masstree.hh>

typedef threadinfo threadinfo_t;

class MtIndex {
 public:
  static constexpr size_t kKeySize = 16;  /// Index key size in bytes
  static_assert(sizeof(MtIndex::kKeySize) % sizeof(uint64_t) == 0, "");

  static constexpr size_t kValueSize = 68;  /// Index value size in bytes
  static_assert(sizeof(MtIndex::kValueSize) % sizeof(uint32_t) == 0, "");

  MtIndex() {}
  ~MtIndex() {}

  inline void setup(threadinfo_t *ti) {
    table_ = new Masstree::default_table();
    table_->initialize(*ti);
  }

  inline void swap_endian(uint8_t *key) {
    auto *key_64 = reinterpret_cast<uint64_t *>(key);
    for (size_t i = 0; i < kKeySize / sizeof(uint64_t); i++) {
      key_64[i] = __bswap_64(key_64[i]);
    }
  }

  // Upsert
  inline void put(uint8_t *key, uint8_t *value, threadinfo_t *ti) {
    swap_endian(key);
    Str key_str(reinterpret_cast<const char *>(key), kKeySize);

    Masstree::default_table::cursor_type lp(table_->table(), key_str);
    const bool found = lp.find_insert(*ti);
    if (!found) {
      ti->observe_phantoms(lp.node());
      qtimes_.ts = ti->update_timestamp();
      qtimes_.prev_ts = 0;
    } else {
      qtimes_.ts = ti->update_timestamp(lp.value()->timestamp());
      qtimes_.prev_ts = lp.value()->timestamp();
      lp.value()->deallocate_rcu(*ti);
    }

    Str value_str(reinterpret_cast<const char *>(value), kValueSize);
    lp.value() = row_type::create1(value_str, qtimes_.ts, *ti);
    lp.finish(1, *ti);
  }

  // Get (unique value)
  inline bool get(uint8_t *key, uint8_t *value, threadinfo_t *ti) {
    swap_endian(key);
    Str key_str(reinterpret_cast<const char *>(key), kKeySize);

    Masstree::default_table::unlocked_cursor_type lp(table_->table(), key_str);
    const bool found = lp.find_unlocked(*ti);
    if (found) memcpy(value, lp.value()->col(0).s, kValueSize);
    return found;
  }

  // An object with callbacks passed to table.scan()
  struct scanner_t {
    scanner_t(size_t range) : range_(range), range_sum_(0) {}

    template <typename SS2, typename K2>
    void visit_leaf(const SS2 &, const K2 &, threadinfo_t &) {}

    bool visit_value(Str, const row_type *row, threadinfo_t &) {
      const size_t value = *reinterpret_cast<const size_t *>(row->col(0).s);
      range_sum_ += value;
      range_--;
      return range_ > 0;
    }

    size_t range_;
    size_t range_sum_;
  };

  /// Return the sum of the values (first eight bytes per value) of \p range
  /// keys including and after \p cur_key
  size_t sum_in_range(uint8_t *cur_key, size_t range, threadinfo_t *ti) {
    if (range == 0) return 0;

    swap_endian(cur_key);
    Str cur_key_str(reinterpret_cast<const char *>(&cur_key), sizeof(size_t));

    scanner_t scanner(range);
    table_->table().scan(cur_key_str, true, scanner, *ti);
    return scanner.range_sum_;
  }

 private:
  Masstree::default_table *table_;
  query<row_type> q_[1];
  loginfo::query_times qtimes_;
};

#endif  // MT_INDEX_API_H
