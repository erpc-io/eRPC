#ifndef MAIN_H
#define MAIN_H

#include <gflags/gflags.h>
#include <stdlib.h>
#include <array>
#include "libhrd_cpp/hrd.h"
#include "sweep.h"

static_assert(is_power_of_two(kAppUnsigBatch), "");

static constexpr size_t kAppNumThreads = kAppNumWorkers / kAppNumMachines;
static constexpr size_t kAppNumQPsPerThread =
    kAppNumMachines * kAppVMsPerMachine;

static constexpr size_t kAppBufSize = MB(2);
static_assert(is_power_of_two(kAppBufSize), "");

// Round RDMA source and target offset address to a cacheline boundary. This
// can increase or decrease performance.
static constexpr bool kAppRoundOffset = true;

// The first kAppWindowSize slots are zeroed out and used for READ completion
// detection. The remaining slots are non-zero and are fetched via READs.
static constexpr size_t kAppPollingRegionSz = kAppWindowSize * kAppRDMASize;
static_assert(kAppPollingRegionSz < kAppBufSize / 10, "");

// static constexpr size_t kAppWindowSize = 32;
// Number of outstanding requests kept by a worker thread  across all QPs.
// This is only used for READs where we can detect completion by polling on
// a READ's destination buffer.
//
// For WRITEs, this is hard to do unless we make every send() signaled. So,
// the number of per-thread outstanding operations per thread with WRITEs is
// O(NUM_CLIENTS * UNSIG_BATCH).

static constexpr size_t kAppMaxPorts = 2;        // Max ports for a thread
static constexpr size_t kAppMaxMachines = 256;   // Max machines in the swarm
static constexpr int kAppWorkerBaseSHMKey = 24;  // SHM keys used by workers

// Checks
static_assert(kAppNumWorkers % kAppNumMachines == 0, "");
static_assert(kAppBufSize >= MB(2), "");  // Large buffer, more parallelism
static_assert(kAppNumMachines >= 2, "");  // At least 2 machines
static_assert(kAppNumWorkers % kAppNumMachines == 0, "");  // kAppNumThreads
static_assert(kHrdSQDepth >= 2 * kAppUnsigBatch, "");  // Queue capacity check

struct thread_params_t {
  size_t wrkr_gid;
  size_t wrkr_lid;
  double* tput_arr;
};

// Flags
DEFINE_uint64(numa_node, 0, "NUMA node");
DEFINE_uint64(use_uc, 0, "Use UC?");
DEFINE_uint64(base_port_index, 0, "Base port index");
DEFINE_uint64(num_ports, 0, "Number of ports");
DEFINE_uint64(machine_id, 0, "ID of this machine");
DEFINE_uint64(do_read, 0, "Use RDMA READs?");

static bool validate_do_read(const char*, uint64_t do_read) {
  // Size checks based on opcode. This is not UD, so no 0-byte read/write.
  if (do_read == 0) {  // For WRITEs
    return kAppRDMASize >= 1 && kAppRDMASize <= kHrdMaxInline;
  } else {
    return kAppRDMASize >= 1 && kAppRDMASize <= kAppBufSize;
  }
}

DEFINE_validator(do_read, &validate_do_read);

// File I/O helpers

// Record machine throughput
void record_sweep_params(FILE* fp) {
  fprintf(fp, "Machine %zu: sweep parameters: ", FLAGS_machine_id);
  fprintf(fp, "kAppRDMASize %zu, ", kAppRDMASize);
  fprintf(fp, "kAppWindowSize %zu, ", kAppWindowSize);
  fprintf(fp, "kAppUnsigBatch %zu, ", kAppUnsigBatch);
  fprintf(fp, "kAppAllsig %u, ", kAppAllsig);
  fprintf(fp, "kAppNumWorkers %zu, ", kAppNumWorkers);
  fflush(fp);
}

// Record machine throughput
void record_machine_tput(FILE* fp, double total_tput) {
  char timebuf[50];
  hrd_get_formatted_time(timebuf);

  fprintf(fp, "Machine %zu: tput = %.2f reqs/s, time %s\n", FLAGS_machine_id,
          total_tput, timebuf);
  fflush(fp);
}

#endif  // MAIN_H
