/**
 * @file apps_common.h
 * @brief Common code for apps
 */
#ifndef APPS_COMMON_H
#define APPS_COMMON_H

#include <gflags/gflags.h>
#include <papi.h>
#include <fstream>
#include "rpc.h"
#include "util/latency.h"

//
// Gflags
//

// Flags that must be used in every app. test_ms and num_machines required in
// the app's config file by the autorun scripts.
DEFINE_uint64(test_ms, 0, "Test milliseconds");
DEFINE_uint64(num_machines, 0, "Number of machines in the cluster");
DEFINE_uint64(machine_id, erpc::kMaxNumMachines, "The ID of this machine");

static bool validate_test_ms(const char *, uint64_t test_ms) {
  return test_ms >= 1000;
}
DEFINE_validator(test_ms, &validate_test_ms);

static bool validate_num_machines(const char *, uint64_t num_machines) {
  return num_machines <= erpc::kMaxNumMachines;
}
DEFINE_validator(num_machines, &validate_num_machines);

static bool validate_machine_id(const char *, uint64_t machine_id) {
  return machine_id < erpc::kMaxNumMachines;
}
DEFINE_validator(machine_id, &validate_machine_id);

// Work around g++-5's unused variable warning for validators
void avoid_gcc5_unused_warning() {
  _unused(test_ms_validator_registered);
  _unused(num_machines_validator_registered);
  _unused(machine_id_validator_registered);
}

//
// PAPI
//
void papi_init() {
  float real_time, proc_time, ipc;
  long long ins;
  int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
  if (ret < PAPI_OK) throw std::runtime_error("PAPI initialization failed.");
}

float papi_get_ipc() {
  float real_time, proc_time, ipc;
  long long ins;
  int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
  if (ret < PAPI_OK) throw std::runtime_error("PAPI measurement failed.");
  return ipc;
}

// Return the control net IP address of the machine with index server_i,
// from the autorun nodes file.
static std::string get_hostname_for_machine(size_t server_i) {
  std::string autorun_node_file = "../eRPC/scripts/autorun_node_file";
  std::ifstream in(autorun_node_file.c_str());

  std::string s;
  s.reserve(100);  // For performance

  for (size_t i = 0; i < server_i; i++) {
    std::getline(in, s);
    erpc::rt_assert(!s.empty(), "Insufficient node names in autorun node file");
  }

  std::getline(in, s);
  erpc::rt_assert(!s.empty(), "Insufficient node names in autorun node file");

  return s;
}

// A basic mempool for preallocated objects of type T
template <class T>
class MemPool {
 public:
  size_t num_to_alloc = 1;
  std::vector<T *> backing_ptr_vec;
  std::vector<T *> pool;

  void extend_pool() {
    T *backing_ptr = new T[num_to_alloc];
    for (size_t i = 0; i < num_to_alloc; i++) pool.push_back(&backing_ptr[i]);
    backing_ptr_vec.push_back(backing_ptr);
    num_to_alloc *= 2;
  }

  T *alloc() {
    if (pool.empty()) extend_pool();
    T *ret = pool.back();
    pool.pop_back();
    return ret;
  }

  void free(T *t) { pool.push_back(t); }

  MemPool() {}
  ~MemPool() {
    for (T *ptr : backing_ptr_vec) delete[] ptr;
  }
};

// A utility class to write stats to /tmp/
class TmpStat {
 public:
  TmpStat() {}

  bool contains_newline(std::string line) {
    for (size_t i = 0; i < line.length(); i++) {
      if (line[i] == '\n') return true;
    }

    return false;
  }

  TmpStat(std::string app_name, std::string header) {
    if (app_name != "small_rpc_tput" && app_name != "large_rpc_tput") {
      throw std::runtime_error("Invalid app.");
    }

    if (contains_newline(header)) {
      throw std::runtime_error("Stats header contains newline.");
    }

    output_file = std::ofstream(std::string("/tmp/") + app_name + "_stats");
    output_file << header << std::endl;
  }

  ~TmpStat() {
    output_file.flush();
    output_file.close();
  }

  void write(std::string stat) { output_file << stat << std::endl; }

 private:
  std::ofstream output_file;
};

// Per-thread application context
class BasicAppContext {
 public:
  TmpStat *tmp_stat = nullptr;
  erpc::Rpc<erpc::IBTransport> *rpc = nullptr;
  erpc::FastRand fastrand;

  std::vector<int> session_num_vec;

  size_t thread_id;         // The ID of the thread that owns this context
  size_t num_sm_resps = 0;  // Number of SM responses

  ~BasicAppContext() {
    if (tmp_stat != nullptr) delete tmp_stat;
  }
};

// A reasonable SM handler
void basic_sm_handler(int session_num, erpc::SmEventType sm_event_type,
                      erpc::SmErrType sm_err_type, void *_context) {
  assert(_context != nullptr);

  auto *c = static_cast<BasicAppContext *>(_context);
  c->num_sm_resps++;

  if (!(sm_event_type == erpc::SmEventType::kConnected ||
        sm_event_type == erpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the eRPC session number - get the index
  size_t session_idx = c->session_num_vec.size();
  for (size_t i = 0; i < c->session_num_vec.size(); i++) {
    if (c->session_num_vec[i] == session_num) session_idx = i;
  }
  erpc::rt_assert(session_idx < c->session_num_vec.size(),
                  "Invalid session number");

  fprintf(stderr,
          "apps_common: Rpc %u: Session number %d (index %zu) %s. Error = %s. "
          "Time elapsed = %.3f s.\n",
          c->rpc->get_rpc_id(), session_num, session_idx,
          erpc::sm_event_type_str(sm_event_type).c_str(),
          erpc::sm_err_type_str(sm_err_type).c_str(),
          c->rpc->sec_since_creation());
}

#endif  // APPS_COMMON_H
