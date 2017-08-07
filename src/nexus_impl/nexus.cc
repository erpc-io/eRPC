#include "nexus.h"
#include <algorithm>
#include "common.h"
#include "rpc.h"
#include "util/barrier.h"
#include "util/misc.h"

namespace ERpc {

template <class TTr>
Nexus<TTr>::Nexus(std::string hostname, uint16_t mgmt_udp_port,
                  size_t num_bg_threads)
    : freq_ghz(measure_rdtsc_freq()),
      hostname(hostname),
      num_bg_threads(num_bg_threads) {
  // Print warning messages if low-performance settings are enabled
  if (kDatapathChecks) {
    LOG_WARN("eRPC Nexus: Datapath checks enabled. Performance will be low.\n");
  }

  if (kFaultInjection) {
    LOG_WARN("eRPC Nexus: Fault injection enabled. Performance will be low.\n");
  }

  rt_assert(num_bg_threads <= kMaxBgThreads, "Too many background threads");

  if (small_rpc_optlevel == small_rpc_optlevel_extreme) {
    rt_assert(num_bg_threads == 0, "Background threads not supported");
  }

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
    bg_thread_ctx.bg_req_list = &bg_req_list[i];

    bg_thread_arr[i] = std::thread(bg_thread_func, bg_thread_ctx);

    // Wait for the launched thread to grab a ERpc thread ID, otherwise later
    // background threads or the foreground thread can grab ID = i.
    while (tls_registry.cur_etid == i) {
      usleep(1);
    }
  }

  // Launch the session management thread
  SmThreadCtx sm_thread_ctx;
  sm_thread_ctx.mgmt_udp_port = mgmt_udp_port;
  sm_thread_ctx.kill_switch = &kill_switch;
  sm_thread_ctx.reg_hooks_arr = const_cast<volatile Hook **>(reg_hooks_arr);
  sm_thread_ctx.nexus_lock = &nexus_lock;
  sm_thread_ctx.sm_tx_list = &sm_tx_list;

  LOG_INFO("eRPC Nexus: Launching session management thread on core %zu.\n",
           kNexusSmThreadCore);
  sm_thread = std::thread(sm_thread_func, sm_thread_ctx);
  bind_to_core(sm_thread, kNexusSmThreadCore);

  LOG_INFO("eRPC Nexus: Created with global UDP port %u, hostname %s.\n",
           mgmt_udp_port, hostname.c_str());
}

template <class TTr>
Nexus<TTr>::~Nexus() {
  LOG_INFO("eRPC Nexus: Destroying Nexus.\n");

  // Signal background and session management threads to kill themselves
  kill_switch = true;

  for (size_t i = 0; i < num_bg_threads; i++) {
    bg_thread_arr[i].join();
  }

  sm_thread.join();
}

template <class TTr>
bool Nexus<TTr>::rpc_id_exists(uint8_t rpc_id) {
  nexus_lock.lock();
  bool ret = (reg_hooks_arr[rpc_id] != nullptr);
  nexus_lock.unlock();
  return ret;
}

template <class TTr>
void Nexus<TTr>::register_hook(Hook *hook) {
  assert(hook != nullptr);

  uint8_t rpc_id = hook->rpc_id;
  assert(rpc_id <= kMaxRpcId);
  assert(reg_hooks_arr[rpc_id] == nullptr);

  nexus_lock.lock();

  req_func_registration_allowed = false;  // Disable future Ops registration
  reg_hooks_arr[rpc_id] = hook;           // Save the hook

  // Install background request submission lists
  for (size_t i = 0; i < num_bg_threads; i++) {
    assert(hook->bg_req_list_arr[i] == nullptr);
    hook->bg_req_list_arr[i] = &bg_req_list[i];
  }

  // Install session managment request submission list
  assert(hook->sm_tx_list == nullptr);
  hook->sm_tx_list = &sm_tx_list;

  nexus_lock.unlock();
}

template <class TTr>
void Nexus<TTr>::unregister_hook(Hook *hook) {
  assert(hook != nullptr);

  uint8_t rpc_id = hook->rpc_id;
  assert(rpc_id <= kMaxRpcId);
  assert(reg_hooks_arr[rpc_id] == hook);
  LOG_INFO("eRPC Nexus: Deregistering Rpc %u.\n", rpc_id);

  nexus_lock.lock();
  reg_hooks_arr[rpc_id] = nullptr;
  nexus_lock.unlock();
}

template <class TTr>
int Nexus<TTr>::register_req_func(uint8_t req_type, ReqFunc app_req_func) {
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

template <class TTr>
double Nexus<TTr>::measure_rdtsc_freq() {
  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);
  uint64_t rdtsc_start = rdtsc();

  // Do not change this loop! The hardcoded value below depends on this loop
  // and prevents it from being optimized out.
  uint64_t sum = 5;
  for (uint64_t i = 0; i < 1000000; i++) {
    sum += i + (sum + i) * (i % sum);
  }
  rt_assert(sum == 13580802877818827968ull, "Error in RDTSC freq measurement");

  clock_gettime(CLOCK_REALTIME, &end);
  uint64_t clock_ns =
      static_cast<uint64_t>(end.tv_sec - start.tv_sec) * 1000000000 +
      static_cast<uint64_t>(end.tv_nsec - start.tv_nsec);
  uint64_t rdtsc_cycles = rdtsc() - rdtsc_start;

  double _freq_ghz = rdtsc_cycles / clock_ns;
  rt_assert(_freq_ghz >= 0.5 && _freq_ghz <= 5.0, "Invalid RDTSC frequency");

  return _freq_ghz;
}

}  // End ERpc
