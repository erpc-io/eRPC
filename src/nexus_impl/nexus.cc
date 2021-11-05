#include "nexus.h"
#include <algorithm>
#include "common.h"
#include "rpc.h"
#include "transport_impl/eth_common.h"
#include "util/autorun_helpers.h"
#include "util/barrier.h"
#include "util/numautils.h"

namespace erpc {

Nexus::Nexus(std::string local_uri, size_t numa_node, size_t num_bg_threads)
    : freq_ghz_(measure_rdtsc_freq()),
      hostname_(extract_hostname_from_uri(local_uri)),
      sm_udp_port_(extract_udp_port_from_uri(local_uri)),
      numa_node_(numa_node),
      num_bg_threads_(num_bg_threads),
      heartbeat_mgr_(hostname_, sm_udp_port_, freq_ghz_,
                     kMachineFailureTimeoutMs) {
  if (kTesting) {
    ERPC_WARN("eRPC Nexus: Testing enabled. Perf will be low.\n");
  }

  rt_assert(sm_udp_port_ >= kBaseSmUdpPort &&
                sm_udp_port_ < (kBaseSmUdpPort + kMaxNumERpcProcesses),
            "Invalid management UDP port");
  rt_assert(num_bg_threads <= kMaxBgThreads, "Too many background threads");
  rt_assert(numa_node < kMaxNumaNodes, "Invalid NUMA node");

  kill_switch_ = false;

  // Launch background threads
  ERPC_INFO("eRPC Nexus: Launching %zu background threads.\n", num_bg_threads);
  for (size_t i = 0; i < num_bg_threads; i++) {
    assert(tls_registry_.cur_etid_ == i);

    BgThreadCtx bg_thread_ctx;
    bg_thread_ctx.kill_switch_ = &kill_switch_;
    bg_thread_ctx.req_func_arr_ = &req_func_arr_;
    bg_thread_ctx.tls_registry_ = &tls_registry_;
    bg_thread_ctx.bg_thread_index_ = i;
    bg_thread_ctx.bg_req_queue_ = &bg_req_queue_[i];

    bg_thread_arr_[i] = std::thread(bg_thread_func, bg_thread_ctx);

    // Wait for the launched thread to grab a eRPC thread ID, otherwise later
    // background threads or the foreground thread can grab ID = i.
    while (tls_registry_.cur_etid_ == i) {
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  }

  // Launch the session management thread
  SmThreadCtx sm_thread_ctx;
  sm_thread_ctx.hostname_ = hostname_;
  sm_thread_ctx.sm_udp_port_ = sm_udp_port_;
  sm_thread_ctx.freq_ghz_ = freq_ghz_;
  sm_thread_ctx.kill_switch_ = &kill_switch_;
  sm_thread_ctx.heartbeat_mgr_ = &heartbeat_mgr_;
  sm_thread_ctx.reg_hooks_arr_ = const_cast<volatile Hook **>(reg_hooks_arr_);
  sm_thread_ctx.reg_hooks_lock_ = &reg_hooks_lock_;

  // Bind the session management thread to the last lcore on numa_node
  size_t sm_thread_lcore_index = num_lcores_per_numa_node() - 1;
  ERPC_INFO("eRPC Nexus: Launching session management thread on core %zu.\n",
            get_lcores_for_numa_node(numa_node).at(sm_thread_lcore_index));
  sm_thread_ = std::thread(sm_thread_func, sm_thread_ctx);
  bind_to_core(sm_thread_, numa_node, sm_thread_lcore_index);

  ERPC_INFO("eRPC Nexus: Created with management UDP port %u, hostname %s.\n",
            sm_udp_port_, hostname_.c_str());
}

Nexus::~Nexus() {
  ERPC_INFO("eRPC Nexus: Destroying Nexus.\n");

  // Signal background and session management threads to kill themselves
  kill_switch_ = true;

  for (size_t i = 0; i < num_bg_threads_; i++) {
    bg_thread_arr_[i].join();
  }

  {
    // The SM thread could be blocked on a recv(), unblock it
    UDPClient<SmPkt> udp_client;
    SmPkt unblock_sm_pkt = SmPkt::make_unblock_req();
    udp_client.send(hostname_, sm_udp_port_, unblock_sm_pkt);
    sm_thread_.join();
  }

  // Reset thread-local storage to prevent errors if gtest reuses the process.
  // Rationale: At this point, eRPC-owned threads are dead. All worker threads
  // should be dead as well, so it's safe to reset TLS.
  for (const Hook *hook : reg_hooks_arr_) {
    if (hook != nullptr) {
      ERPC_ERROR(
          "eRPC Nexus: Nexus destroyed while an Rpc object depending on this "
          "Nexus is still alive.");
      assert(false);  // Die in debug mode
    }
  }

  tls_registry_.reset();
}

bool Nexus::rpc_id_exists(uint8_t rpc_id) {
  reg_hooks_lock_.lock();
  bool ret = (reg_hooks_arr_[rpc_id] != nullptr);
  reg_hooks_lock_.unlock();
  return ret;
}

void Nexus::register_hook(Hook *hook) {
  uint8_t rpc_id = hook->rpc_id_;
  assert(rpc_id <= kMaxRpcId);
  assert(reg_hooks_arr_[rpc_id] == nullptr);

  reg_hooks_lock_.lock();

  req_func_registration_allowed_ = false;  // Disable future Ops registration
  reg_hooks_arr_[rpc_id] = hook;           // Save the hook

  // Install background request submission lists
  for (size_t i = 0; i < num_bg_threads_; i++) {
    hook->bg_req_queue_arr_[i] = &bg_req_queue_[i];
  }

  reg_hooks_lock_.unlock();
}

void Nexus::unregister_hook(Hook *hook) {
  uint8_t rpc_id = hook->rpc_id_;
  assert(rpc_id <= kMaxRpcId);
  assert(reg_hooks_arr_[rpc_id] == hook);
  ERPC_INFO("eRPC Nexus: Deregistering Rpc %u.\n", rpc_id);

  reg_hooks_lock_.lock();
  reg_hooks_arr_[rpc_id] = nullptr;
  reg_hooks_lock_.unlock();
}

int Nexus::register_req_func(uint8_t req_type, erpc_req_func_t req_func,
                             ReqFuncType req_func_type) {
  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Nexus: Failed to register handlers for request type %u. Issue",
          req_type);

  // If any Rpc is already registered, the user cannot register new Ops
  if (!req_func_registration_allowed_) {
    ERPC_WARN("%s: Registration not allowed anymore.\n", issue_msg);
    return -EPERM;
  }

  ReqFunc &arr_req_func = req_func_arr_[req_type];

  if (req_func_arr_[req_type].is_registered()) {
    ERPC_WARN("%s: Handler for this request type already exists.\n", issue_msg);
    return -EEXIST;
  }

  if (req_func == nullptr) {
    ERPC_WARN("%s: Invalid handler.\n", issue_msg);
    return -EINVAL;
  }

  if (req_func_type == ReqFuncType::kBackground && num_bg_threads_ == 0) {
    ERPC_WARN("%s: Background threads not available.\n", issue_msg);
    return -EPERM;
  }

  arr_req_func = ReqFunc(req_func, req_func_type);
  return 0;
}
}  // namespace erpc
