#include "mt_index_api.h"

volatile uint64_t globalepoch = 1;
volatile uint64_t active_epoch = 1;
volatile bool recovering = false;
kvtimestamp_t initial_timestamp;
kvepoch_t global_log_epoch = 0;
