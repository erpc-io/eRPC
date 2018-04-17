/**
 * @file apps_common.h
 * @brief Common code for apps
 */
#ifndef APPS_COMMON_H
#define APPS_COMMON_H

#include <gflags/gflags.h>
#include <boost/algorithm/string.hpp>
#include <fstream>
#include "rpc.h"
#include "util/latency.h"

//
// Gflags
//

// Flags that must be used in every app. test_ms and num_processes are required
// in the app's config file by the autorun scripts.
DEFINE_uint64(test_ms, 0, "Test milliseconds");
DEFINE_uint64(sm_verbose, 0, "Print session management debug info");
DEFINE_uint64(num_processes, 0, "Number of eRPC processes in the cluster");
DEFINE_uint64(process_id, SIZE_MAX, "The global ID of this process");
DEFINE_uint64(numa_node, 0, "NUMA node for this process");
DEFINE_string(numa_0_ports, "", "Fabric ports on NUMA node 0, CSV, no spaces");
DEFINE_string(numa_1_ports, "", "Fabric ports on NUMA node 1, CSV, no spaces");

// Return the fabric ports for a NUMA node. The user must specify numa_0_ports
// and numa_1_ports, but they may be empty.
std::vector<size_t> flags_get_numa_ports(size_t numa_node) {
  erpc::rt_assert(numa_node <= 1);  // Only NUMA 0 and 1 supported for now
  std::vector<size_t> ret;

  std::string port_str =
      numa_node == 0 ? FLAGS_numa_0_ports : FLAGS_numa_1_ports;
  if (port_str.size() == 0) return ret;

  std::vector<std::string> split_vec;
  boost::split(split_vec, port_str, boost::is_any_of(","));
  erpc::rt_assert(split_vec.size() > 0);

  for (auto &s : split_vec) {
    boost::trim(s);
    ret.push_back(std::stoull(s));
  }

  return ret;
}

// A basic mempool for preallocated objects of type T. eRPC has a faster,
// hugepage-backed one.
template <class T>
class AppMemPool {
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

  AppMemPool() {}
  ~AppMemPool() {
    for (T *ptr : backing_ptr_vec) delete[] ptr;
  }
};

// A utility class to write stats to /tmp/
class TmpStat {
 public:
  TmpStat() {}

  TmpStat(std::string header) {
    erpc::rt_assert(!contains_newline(header), "Invalid stat file header");
    char *autorun_app = std::getenv("autorun_app");
    erpc::rt_assert(autorun_app != nullptr, "autorun_app environment invalid");

    auto filename = std::string("/tmp/") + autorun_app + "_stats_" +
                    std::to_string(FLAGS_process_id);

    printf("Writing stats to file %s\n", filename.c_str());
    stat_file = std::ofstream(filename);
    stat_file << header << std::endl;
  }

  ~TmpStat() {
    stat_file.flush();
    stat_file.close();
  }

  void write(std::string stat) { stat_file << stat << std::endl; }

 private:
  bool contains_newline(std::string line) {
    for (size_t i = 0; i < line.length(); i++) {
      if (line[i] == '\n') return true;
    }
    return false;
  }

  std::ofstream stat_file;
};

// Per-thread application context
class BasicAppContext {
 public:
  TmpStat *tmp_stat = nullptr;
  erpc::Rpc<erpc::CTransport> *rpc = nullptr;
  erpc::FastRand fastrand;

  std::vector<int> session_num_vec;

  size_t thread_id;         // The ID of the thread that owns this context
  size_t num_sm_resps = 0;  // Number of SM responses

  ~BasicAppContext() {
    if (tmp_stat != nullptr) delete tmp_stat;
  }
};

// A basic session management handler that expects successful responses
void basic_sm_handler(int session_num, erpc::SmEventType sm_event_type,
                      erpc::SmErrType sm_err_type, void *_context) {
  auto *c = static_cast<BasicAppContext *>(_context);
  c->num_sm_resps++;

  erpc::rt_assert(sm_err_type == erpc::SmErrType::kNoError,
                  "SM response with error");

  if (!(sm_event_type == erpc::SmEventType::kConnected ||
        sm_event_type == erpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the eRPC session number - get the index in vector
  size_t session_idx = c->session_num_vec.size();
  for (size_t i = 0; i < c->session_num_vec.size(); i++) {
    if (c->session_num_vec[i] == session_num) session_idx = i;
  }

  erpc::rt_assert(session_idx < c->session_num_vec.size(),
                  "SM callback for invalid session number.");

  if (FLAGS_sm_verbose == 1) {
    fprintf(stderr,
            "Process %zu, Rpc %u: Session number %d (index %zu) %s. Error %s. "
            "Time elapsed = %.3f s.\n",
            FLAGS_process_id, c->rpc->get_rpc_id(), session_num, session_idx,
            erpc::sm_event_type_str(sm_event_type).c_str(),
            erpc::sm_err_type_str(sm_err_type).c_str(),
            c->rpc->sec_since_creation());
  }
}

#endif  // APPS_COMMON_H
