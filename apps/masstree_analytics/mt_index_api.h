#ifndef MT_INDEX_API_H
#define MT_INDEX_API_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "json.hh"
#include "kvrow.hh"
#include "kvthread.hh"
#include "masstree.hh"
#include "masstree_insert.hh"
#include "masstree_remove.hh"
#include "masstree_scan.hh"
#include "masstree_tcursor.hh"
#include "query_masstree.hh"
#pragma GCC diagnostic pop

typedef threadinfo threadinfo_t;

class MtIndex {
 public:
  MtIndex() {}
  ~MtIndex() {}

  inline void setup(threadinfo_t *ti) {
    table_ = new Masstree::default_table();
    table_->initialize(*ti);
  }

  inline void swap_endian(uint64_t &x) { x = __bswap_64(x); }

  // Upsert
  inline void put(size_t key, size_t value, threadinfo_t *ti) {
    swap_endian(key);
    Str key_str(reinterpret_cast<const char *>(&key), sizeof(size_t));

    Masstree::default_table::cursor_type lp(table_->table(), key_str);
    bool found = lp.find_insert(*ti);
    if (!found) {
      ti->observe_phantoms(lp.node());
      qtimes_.ts = ti->update_timestamp();
      qtimes_.prev_ts = 0;
    } else {
      qtimes_.ts = ti->update_timestamp(lp.value()->timestamp());
      qtimes_.prev_ts = lp.value()->timestamp();
      lp.value()->deallocate_rcu(*ti);
    }

    Str value_str(reinterpret_cast<const char *>(&value), sizeof(size_t));

    lp.value() = row_type::create1(value_str, qtimes_.ts, *ti);
    lp.finish(1, *ti);
  }

  // Get (unique value)
  inline bool get(size_t key, size_t &value, threadinfo_t *ti) {
    swap_endian(key);
    Str key_str(reinterpret_cast<const char *>(&key), sizeof(size_t));

    Masstree::default_table::unlocked_cursor_type lp(table_->table(), key_str);
    bool found = lp.find_unlocked(*ti);

    if (found) {
      value = *reinterpret_cast<const size_t *>(lp.value()->col(0).s);
    }

    return found;
  }

  // An object with callbacks passed to table.scan()
  struct scanner_t {
    scanner_t(size_t range) : range(range), range_sum(0) {}

    template <typename SS2, typename K2>
    void visit_leaf(const SS2 &, const K2 &, threadinfo_t &) {}

    bool visit_value(Str, const row_type *row, threadinfo_t &) {
      size_t value = *reinterpret_cast<const size_t *>(row->col(0).s);
      range_sum += value;
      range--;
      return range > 0;
    }

    size_t range;
    size_t range_sum;
  };

  /// Return the sum of \p range keys including and after \p cur_key
  size_t sum_in_range(size_t cur_key, size_t range, threadinfo_t *ti) {
    if (range == 0) return 0;

    swap_endian(cur_key);
    Str cur_key_str(reinterpret_cast<const char *>(&cur_key), sizeof(size_t));

    scanner_t scanner(range);
    table_->table().scan(cur_key_str, true, scanner, *ti);
    return scanner.range_sum;
  }

 private:
  Masstree::default_table *table_;
  query<row_type> q_[1];
  loginfo::query_times qtimes_;
};

#endif  // MT_INDEX_API_H
