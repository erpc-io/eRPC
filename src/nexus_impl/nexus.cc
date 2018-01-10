#include "nexus.h"
#include <algorithm>
#include "common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/barrier.h"
#include "util/misc.h"

namespace erpc {

Nexus::Nexus(std::string local_uri, uint8_t epid, size_t numa_node,
             size_t num_bg_threads)
    : freq_ghz(measure_rdtsc_freq()),
      hostname(extract_hostname_from_uri(local_uri)),
      sm_udp_port(std::stoi(extract_udp_port_from_uri(local_uri))),
      epid(epid),
      numa_node(numa_node),
      num_bg_threads(num_bg_threads) {
  if (kTesting) {
    LOG_WARN("eRPC Nexus: Testing enabled. Perf will be low.\n");
  }
  rt_assert(num_bg_threads <= kMaxBgThreads, "Too many background threads");
  rt_assert(epid <= kMaxEPid, "Invalid eRPC PID");
  rt_assert(numa_node < kInvalidNUMANode, "Invalid NUMA node");

  kill_switch = false;

  // Launch background threads
  LOG_INFO("eRPC Nexus: Launching %zu background threads.\n", num_bg_threads);
  for (size_t i = 0; i < num_bg_threads; i++) {
    assert(tls_registry.cur_etid == i);

    BgThreadCtx bg_thread_ctx;
    bg_thread_ctx.kill_switch = &kill_switch;
    bg_thread_ctx.req_func_arr = &req_func_arr;
    bg_thread_ctx.tls_registry = &tls_registry;
    bg_thread_ctx.bg_thread_index = i;
    bg_thread_ctx.bg_req_queue = &bg_req_queue[i];

    bg_thread_arr[i] = std::thread(bg_thread_func, bg_thread_ctx);

    // Wait for the launched thread to grab a eRPC thread ID, otherwise later
    // background threads or the foreground thread can grab ID = i.
    while (tls_registry.cur_etid == i) usleep(1);
  }

  // Launch the session management thread
  SmThreadCtx sm_thread_ctx;
  sm_thread_ctx.hostname = hostname;
  sm_thread_ctx.sm_udp_port = sm_udp_port;
  sm_thread_ctx.kill_switch = &kill_switch;
  sm_thread_ctx.reg_hooks_arr = const_cast<volatile Hook **>(reg_hooks_arr);
  sm_thread_ctx.nexus_lock = &nexus_lock;

  LOG_INFO("eRPC Nexus: Launching session management thread on core %zu.\n",
           kNexusSmThreadCore);
  sm_thread = std::thread(sm_thread_func, sm_thread_ctx);
  bind_to_core(sm_thread, 0, kNexusSmThreadCore);  // NUMA 0

  LOG_INFO("eRPC Nexus: Created with UDP port %u, hostname %s.\n", sm_udp_port,
           hostname.c_str());
}

Nexus::~Nexus() {
  LOG_INFO("eRPC Nexus: Destroying Nexus.\n");

  // Signal background and session management threads to kill themselves
  kill_switch = true;
  for (size_t i = 0; i < num_bg_threads; i++) bg_thread_arr[i].join();
  sm_thread.join();

  // Reset thread-local storage to prevent errors if gtest reuses the process.
  // Rationale: At this point, eRPC-owned threads are dead. All worker threads
  // should be dead as well, so it's safe to reset TLS.
  for (const Hook *hook : reg_hooks_arr) {
    if (hook != nullptr) {
      LOG_WARN("eRPC Rpc: Deleting Nexus, but a worker is still registered");
      assert(false);  // Die in debug mode
    }
  }

  tls_registry.reset();
}

bool Nexus::rpc_id_exists(uint8_t rpc_id) {
  nexus_lock.lock();
  bool ret = (reg_hooks_arr[rpc_id] != nullptr);
  nexus_lock.unlock();
  return ret;
}

void Nexus::register_hook(Hook *hook) {
  uint8_t rpc_id = hook->rpc_id;
  assert(rpc_id <= kMaxRpcId);
  assert(reg_hooks_arr[rpc_id] == nullptr);

  nexus_lock.lock();

  req_func_registration_allowed = false;  // Disable future Ops registration
  reg_hooks_arr[rpc_id] = hook;           // Save the hook

  // Install background request submission lists
  for (size_t i = 0; i < num_bg_threads; i++) {
    hook->bg_req_queue_arr[i] = &bg_req_queue[i];
  }

  nexus_lock.unlock();
}

void Nexus::unregister_hook(Hook *hook) {
  uint8_t rpc_id = hook->rpc_id;
  assert(rpc_id <= kMaxRpcId);
  assert(reg_hooks_arr[rpc_id] == hook);
  LOG_INFO("eRPC Nexus: Deregistering Rpc %u.\n", rpc_id);

  nexus_lock.lock();
  reg_hooks_arr[rpc_id] = nullptr;
  nexus_lock.unlock();
}

int Nexus::register_req_func(uint8_t req_type, ReqFunc app_req_func) {
  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Nexus: Failed to register handlers for request type %u. Issue",
          req_type);

  // If any Rpc is already registered, the user cannot register new Ops
  if (!req_func_registration_allowed) {
    LOG_WARN("%s: Registration not allowed anymore.\n", issue_msg);
    return -EPERM;
  }

  ReqFunc &arr_req_func = req_func_arr[req_type];

  // Check if this request type is already registered
  if (req_func_arr[req_type].is_registered()) {
    LOG_WARN("%s: A handler for this request type already exists.\n",
             issue_msg);
    return -EEXIST;
  }

  // Check if the application's Ops is valid
  if (app_req_func.req_func == nullptr) {
    LOG_WARN("%s: Invalid handler.\n", issue_msg);
    return -EINVAL;
  }

  // If the request handler runs in the background, we must have bg threads
  if (app_req_func.is_background() && num_bg_threads == 0) {
    LOG_WARN("%s: Background threads not available.\n", issue_msg);
    return -EPERM;
  }

  arr_req_func = app_req_func;
  return 0;
}
}  // End erpc
