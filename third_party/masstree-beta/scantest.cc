#include "query_masstree.hh"

using namespace Masstree;

kvepoch_t global_log_epoch = 0;
volatile mrcu_epoch_type globalepoch = 1; // global epoch, updated by main thread regularly
volatile bool recovering = false; // so don't add log entries, and free old value immediately
kvtimestamp_t initial_timestamp;

int
main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;

    threadinfo* ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    default_table::test(*ti);
}
