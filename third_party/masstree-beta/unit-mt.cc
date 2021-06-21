#include <iostream>
#include <random>
#include <vector>
#include <thread>

#include <pthread.h>

#include "config.h"
#include "compiler.hh"

#include "masstree.hh"
#include "kvthread.hh"
#include "masstree_tcursor.hh"
#include "masstree_insert.hh"
#include "masstree_print.hh"
#include "masstree_remove.hh"
#include "masstree_scan.hh"
#include "masstree_stats.hh"
#include "string.hh"

#define NUM_THREADS 64

class key_unparse_unsigned {
public:
    static int unparse_key(Masstree::key<uint64_t> key, char* buf, int buflen) {
        return snprintf(buf, buflen, "%" PRIu64, key.ikey());
    }
};

class MasstreeWrapper {
public:
    static constexpr uint64_t insert_bound = 0xfffff; //0xffffff;
    struct table_params : public Masstree::nodeparams<15,15> {
        typedef uint64_t value_type;
        typedef Masstree::value_print<value_type> value_print_type;
        typedef threadinfo threadinfo_type;
        typedef key_unparse_unsigned key_unparse_type;
        static constexpr ssize_t print_max_indent_depth = 12;
    };

    typedef Masstree::Str Str;
    typedef Masstree::basic_table<table_params> table_type;
    typedef Masstree::unlocked_tcursor<table_params> unlocked_cursor_type;
    typedef Masstree::tcursor<table_params> cursor_type;
    typedef Masstree::leaf<table_params> leaf_type;
    typedef Masstree::internode<table_params> internode_type;

    typedef typename table_type::node_type node_type;
    typedef typename unlocked_cursor_type::nodeversion_value_type nodeversion_value_type;

    static __thread typename table_params::threadinfo_type *ti;

    MasstreeWrapper() {
        this->table_init();
    }

    void table_init() {
        if (ti == nullptr)
            ti = threadinfo::make(threadinfo::TI_MAIN, -1);
        table_.initialize(*ti);
        key_gen_ = 0;
    }

    void keygen_reset() {
        key_gen_ = 0;
    }

    static void thread_init(int thread_id) {
        if (ti == nullptr)
            ti = threadinfo::make(threadinfo::TI_PROCESS, thread_id);
    }

    void insert_test() {
        while (1) {
            auto int_key = fetch_and_add(&key_gen_, 1);
            uint64_t key_buf;
            if (int_key > insert_bound)
                break;
            Str key = make_key(int_key, key_buf);

            cursor_type lp(table_, key);
            bool found = lp.find_insert(*ti);
            always_assert(!found, "keys should all be unique");

            lp.value() = int_key;

            fence();
            lp.finish(1, *ti);
        }
    }

    void remove_test() {
        while (1) {
            auto int_key = fetch_and_add(&key_gen_, 1);
            uint64_t key_buf;
            if (int_key > insert_bound)
                break;
            Str key = make_key(int_key, key_buf);

            cursor_type lp(table_, key);
            bool found = lp.find_locked(*ti);
            always_assert(found, "keys must all exist");
            lp.finish(-1, *ti);
        }
    }

    void insert_remove_test(int thread_id) {
        std::mt19937 gen(thread_id);
        std::uniform_int_distribution<int> dist(1, 6);
        uint64_t int_key = 0;
        bool need_print = true;
        while (!stopping) {
            int_key = fetch_and_add(&key_gen_, 1);
            uint64_t key_buf;
            if (int_key > insert_bound)
                break;
            Str key = make_key(int_key, key_buf);

            cursor_type lp(table_, key);
            bool found = lp.find_insert(*ti);
            always_assert(!found, "keys should all be unique 1");

            lp.value() = int_key;
            fence();
            lp.finish(1, *ti);

            if (dist(gen) <= 2) {
                cursor_type lp1(table_, key);
                bool found1 = lp1.find_locked(*ti);
                if (!found1) {
                    stopping = true;
                    lp1.finish(0, *ti);
                    printf("failed at key %" PRIu64 ", lp1 got %p\n", int_key, lp1.node());
                    need_print = true;
                    break;
                    always_assert(found1, "this is my key!");
                } else {
                    lp1.finish(-1, *ti);
                }
            }
        }
        printf("stopped at key %" PRIu64 "\n", int_key);
        if (need_print && fetch_and_add(&printing, 1) == 0) {
            table_.print(stdout);
            fflush(stdout);
            fprintf(stdout, "Stats: %s\n",
                    Masstree::json_stats(table_, ti).unparse(lcdf::Json::indent_depth(1000)).c_str());
        }
    }

private:
    table_type table_;
    uint64_t key_gen_;
    static bool stopping;
    static uint32_t printing;

    static inline Str make_key(uint64_t int_key, uint64_t& key_buf) {
        key_buf = __builtin_bswap64(int_key);
        return Str((const char *)&key_buf, sizeof(key_buf));
    }
};

__thread typename MasstreeWrapper::table_params::threadinfo_type* MasstreeWrapper::ti = nullptr;
bool MasstreeWrapper::stopping = false;
uint32_t MasstreeWrapper::printing = 0;

volatile mrcu_epoch_type active_epoch = 1;
volatile uint64_t globalepoch = 1;
volatile bool recovering = false;

void test_thread(MasstreeWrapper* mt, int thread_id) {
    mt->thread_init(thread_id);
    mt->insert_remove_test(thread_id);
}

int main() {
    auto mt = new MasstreeWrapper();
    mt->keygen_reset();
    std::cout << "insert_remove_test..." << std::endl;

    std::vector<std::thread> ths;

    for (int i = 0; i < NUM_THREADS; ++i)
        ths.emplace_back(test_thread, mt, i);
    for (auto& t : ths)
        t.join();

    std::cout << "test pass." << std::endl;
    return 0;
}
