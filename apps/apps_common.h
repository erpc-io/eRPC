/**
 * @file apps_common.h
 * @brief Common code for apps
 */
#pragma once

#include <gflags/gflags.h>
#include <set>
#include "rpc.h"
#include "util/latency.h"

#define Ki(x) (static_cast<size_t>(x) * 1000)
#define Mi(x) (static_cast<size_t>(x) * 1000 * 1000)
#define Gi(x) (static_cast<size_t>(x) * 1000 * 1000 * 1000)

// Gflags

// Flags that must be used in every app. test_ms and num_processes are required
// in the app's config file by the autorun scripts.
DEFINE_uint64(test_ms, 0, "Test milliseconds");
DEFINE_uint64(sm_verbose, 0, "Print session management debug info");
DEFINE_uint64(num_processes, 0, "Number of eRPC processes in the cluster");
DEFINE_uint64(process_id, SIZE_MAX, "The global ID of this process");
DEFINE_uint64(numa_node, 0, "NUMA node for this process");
DEFINE_string(numa_0_ports, "", "Fabric ports on NUMA node 0, CSV, no spaces");
DEFINE_string(numa_1_ports, "", "Fabric ports on NUMA node 1, CSV, no spaces");

/// Return the fabric ports for a NUMA node. The user must specify numa_0_ports
/// and numa_1_ports, but they may be empty.
std::vector<size_t> flags_get_numa_ports(size_t numa_node) {
  erpc::rt_assert(numa_node <= 1);  // Only NUMA 0 and 1 supported for now
  std::vector<size_t> ret;

  std::string port_str =
      numa_node == 0 ? FLAGS_numa_0_ports : FLAGS_numa_1_ports;
  if (port_str.size() == 0) return ret;

  std::vector<std::string> split_vec = erpc::split(port_str, ',');
  erpc::rt_assert(split_vec.size() > 0);

  for (auto &s : split_vec) ret.push_back(std::stoull(s));  // stoull trims ' '

  return ret;
}

/// A basic mempool for preallocated objects of type T. eRPC has a faster,
/// hugepage-backed one.
template <class T>
class AppMemPool {
 public:
  size_t num_to_alloc_ = 1;
  std::vector<T *> backing_ptr_vec_;
  std::vector<T *> pool_;

  void extend_pool() {
    T *backing_ptr = new T[num_to_alloc_];
    for (size_t i = 0; i < num_to_alloc_; i++) pool_.push_back(&backing_ptr[i]);
    backing_ptr_vec_.push_back(backing_ptr);
    num_to_alloc_ *= 2;
  }

  T *alloc() {
    if (pool_.empty()) extend_pool();
    T *ret = pool_.back();
    pool_.pop_back();
    return ret;
  }

  void free(T *t) { pool_.push_back(t); }

  AppMemPool() {}
  ~AppMemPool() {
    for (T *ptr : backing_ptr_vec_) delete[] ptr;
  }
};

/// A utility class to write stats to /tmp
class TmpStat {
 public:
  TmpStat() {}

  TmpStat(std::string header) {
    erpc::rt_assert(!contains_newline(header), "Invalid stat file header");
    char *autorun_app = std::getenv("autorun_app");
    erpc::rt_assert(autorun_app != nullptr, "autorun_app environment invalid");

#ifndef _WIN32
    auto filename = std::string("/tmp/") + autorun_app + "_stats_" +
                    std::to_string(FLAGS_process_id);
#else
    // Without the /tmp prefix
    auto filename =
        std::string(autorun_app) + "_stats_" + std::to_string(FLAGS_process_id);
#endif

    printf("Writing stats to file %s\n", filename.c_str());
    stat_file_ = fopen(filename.c_str(), "w");
    erpc::rt_assert(stat_file_ != nullptr, "Failed to open stat file");

    fprintf(stat_file_, "%s\n", header.c_str());
    fflush(stat_file_);
  }

  ~TmpStat() {
    fflush(stat_file_);
    fclose(stat_file_);
  }

  void write(std::string stat) {
    fprintf(stat_file_, "%s\n", stat.c_str());
    fflush(stat_file_);
  }

 private:
  bool contains_newline(std::string line) {
    for (size_t i = 0; i < line.length(); i++) {
      if (line[i] == '\n') return true;
    }
    return false;
  }

  FILE *stat_file_;
};

/// Base class for per-thread application context
class BasicAppContext {
 public:
  TmpStat *tmp_stat_ = nullptr;
  erpc::Rpc<erpc::CTransport> *rpc_ = nullptr;
  erpc::FastRand fastrand_;

  std::vector<int> session_num_vec_;

  size_t thread_id_;           // The ID of the thread that owns this context
  size_t num_sm_resps_ = 0;    // Number of SM responses
  bool ping_pending_ = false;  // Only one ping is allowed at a time

  ~BasicAppContext() {
    if (tmp_stat_ != nullptr) delete tmp_stat_;
  }

  // Use Lemire's trick to get a random session number from session_num_vec
  inline int fast_get_rand_session_num() {
    uint32_t x = fastrand_.next_u32();
    size_t rand_index =
        (static_cast<size_t>(x) * session_num_vec_.size()) >> 32;
    return session_num_vec_[rand_index];
  }
};

/// A basic session management handler that expects successful responses
void basic_sm_handler(int session_num, erpc::SmEventType sm_event_type,
                      erpc::SmErrType sm_err_type, void *_context) {
  auto *c = static_cast<BasicAppContext *>(_context);
  c->num_sm_resps_++;

  erpc::rt_assert(
      sm_err_type == erpc::SmErrType::kNoError,
      "SM response with error " + erpc::sm_err_type_str(sm_err_type));

  if (!(sm_event_type == erpc::SmEventType::kConnected ||
        sm_event_type == erpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the eRPC session number - get the index in vector
  size_t session_idx = c->session_num_vec_.size();
  for (size_t i = 0; i < c->session_num_vec_.size(); i++) {
    if (c->session_num_vec_[i] == session_num) session_idx = i;
  }

  erpc::rt_assert(session_idx < c->session_num_vec_.size(),
                  "SM callback for invalid session number.");

  if (FLAGS_sm_verbose == 1) {
    fprintf(stderr,
            "Process %zu, Rpc %u: Session number %d (index %zu) %s. Error %s. "
            "Time elapsed = %.3f s.\n",
            FLAGS_process_id, c->rpc_->get_rpc_id(), session_num, session_idx,
            erpc::sm_event_type_str(sm_event_type).c_str(),
            erpc::sm_err_type_str(sm_err_type).c_str(),
            c->rpc_->sec_since_creation());
  }
}

// Utility pings
static constexpr size_t kPingMsgSize = 32;
static constexpr uint8_t kPingReqHandlerType = 201;
static constexpr uint8_t kPingEvLoopMs = 1;
static constexpr uint8_t kPingTimeoutMs = 50;

/// Apps must register this request handler with type = kPingReqHandlerType to
/// support pings
void ping_req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<BasicAppContext *>(_context);

  erpc::MsgBuffer &resp_msgbuf = req_handle->pre_resp_msgbuf_;
  c->rpc_->resize_msg_buffer(&resp_msgbuf, kPingMsgSize);
  c->rpc_->enqueue_response(req_handle, &resp_msgbuf);
}

void ping_cont_func(void *_context, void *) {
  auto *c = static_cast<BasicAppContext *>(_context);
  c->ping_pending_ = false;  // Mark ping as completed
}

/// Ping all sessions after connecting them
void ping_all_blocking(BasicAppContext &c) {
  std::set<std::string> hostname_set;
  erpc::MsgBuffer ping_req, ping_resp;

  // These buffers stay valid for the lifetime of the ping RPC
  ping_req = c.rpc_->alloc_msg_buffer_or_die(kPingMsgSize);
  ping_resp = c.rpc_->alloc_msg_buffer_or_die(kPingMsgSize);

  for (int &session_num : c.session_num_vec_) {
    auto srv_hostname = c.rpc_->get_remote_hostname(session_num);
    if (hostname_set.count(srv_hostname) > 0) continue;
    hostname_set.insert(srv_hostname);

    printf("Process %zu, thread %zu: Pinging server %s.\n", FLAGS_process_id,
           c.thread_id_, srv_hostname.c_str());

    c.ping_pending_ = true;
    c.rpc_->enqueue_request(session_num, kPingReqHandlerType, &ping_req,
                            &ping_resp, ping_cont_func, nullptr);

    size_t ms_elapsed = 0;
    while (c.ping_pending_) {
      c.rpc_->run_event_loop(kPingEvLoopMs);
      ms_elapsed += kPingEvLoopMs;
      if (ms_elapsed > kPingTimeoutMs) {
        printf("Process %zu, thread %zu: Fabric to server broken %s.\n",
               FLAGS_process_id, c.thread_id_, srv_hostname.c_str());
        break;
      }
    }
  }
}
