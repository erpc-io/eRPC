/**
 * @file apps_common.h
 * @brief Common code for apps
 */
#ifndef APPS_COMMON_H
#define APPS_COMMON_H

#include <gflags/gflags.h>
#include <fstream>
#include "rpc.h"

// Flags that must be used in every app. test_ms and num_machines required in
// the app's config file by the autorun scripts.
DEFINE_uint64(test_ms, 0, "Test milliseconds");
DEFINE_uint64(num_machines, 0, "Number of machines in the cluster");
DEFINE_uint64(machine_id, ERpc::kMaxNumMachines, "The ID of this machine");

static bool validate_test_ms(const char *, uint64_t test_ms) {
  return test_ms >= 1000;
}
DEFINE_validator(test_ms, &validate_test_ms);

static bool validate_num_machines(const char *, uint64_t num_machines) {
  return num_machines <= ERpc::kMaxNumMachines;
}
DEFINE_validator(num_machines, &validate_num_machines);

static bool validate_machine_id(const char *, uint64_t machine_id) {
  return machine_id < ERpc::kMaxNumMachines;
}
DEFINE_validator(machine_id, &validate_machine_id);

// Work around g++-5's unused variable warning for validators
void avoid_gcc5_unused_warning() {
  _unused(test_ms_validator_registered);
  _unused(num_machines_validator_registered);
  _unused(machine_id_validator_registered);
}

namespace ERpc {
// A utility class to write stats to /tmp/
class TmpStat {
 public:
  TmpStat() {}

  TmpStat(std::string app_name) {
    // Add your app name here
    assert(app_name == "small_rpc_tput" || app_name == "large_rpc_tput");
    output_file = std::ofstream(std::string("/tmp/") + app_name + "_stats");
  }

  ~TmpStat() {
    output_file.flush();
    output_file.close();
  }

  void write(std::string stat) { output_file << stat << std::endl; }

 private:
  std::ofstream output_file;
};

}  // End ERpc

#endif  // APPS_COMMON_H
