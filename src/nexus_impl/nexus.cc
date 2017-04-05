#include "nexus.h"
#include <algorithm>
#include "common.h"
#include "rpc.h"
#include "util/barrier.h"
#include "util/gettid.h"

namespace ERpc {

template <class TTr>
Nexus<TTr>::Nexus(uint16_t mgmt_udp_port, size_t num_bg_threads,
                  double udp_drop_prob)
    : udp_config(mgmt_udp_port, udp_drop_prob),
      freq_ghz(get_freq_ghz()),
      hostname(get_hostname()),
      num_bg_threads(num_bg_threads) {
  // Print warning messages if low-performance debug settings are enabled
  if (kDatapathVerbose) {
    fprintf(stderr,
            "eRPC Nexus: Datapath verbose enabled. Performance will be low.\n");
  }

  if (kDatapathChecks) {
    fprintf(stderr,
            "eRPC Nexus: Datapath checks enabled. Performance will be low.\n");
  }

  if (num_bg_threads > kMaxBgThreads) {
    throw std::runtime_error("eRPC Nexus: Too many background threads.");
  }

  if (udp_drop_prob > kMaxUdpDropProb) {
    throw std::runtime_error("eRPC Nexus: UDP drop probability too high.");
  }

  if (small_rpc_optlevel == small_rpc_optlevel_extreme && num_bg_threads > 0) {
    throw std::runtime_error(
        "eRPC Nexus: Background threads not supported with "
        "small_rpc_optlevel_extreme.");
  }

  // Launch the session management thread
  erpc_dprintf_noargs("eRPC Nexus: Launching session management thread.\n");
  sm_kill_switch = false;
  sm_thread =
      std::thread(sm_thread_func, (volatile bool *)&sm_kill_switch,
                  (volatile Hook **)reg_hooks_arr, &nexus_lock, &udp_config);

  // Launch background threads
  erpc_dprintf("eRPC Nexus: Launching %zu background threads.\n",
               num_bg_threads);
  bg_kill_switch = false;
  for (size_t i = 0; i < num_bg_threads; i++) {
    bg_thread_ctx_arr[i].req_func_arr = &req_func_arr;
    bg_thread_ctx_arr[i].bg_kill_switch = &bg_kill_switch;
    bg_thread_ctx_arr[i].bg_thread_id = i;

    bg_thread_arr[i] = std::thread(bg_thread_func, &bg_thread_ctx_arr[i]);
  }

  erpc_dprintf("eRPC Nexus: Created with global UDP port %u, hostname %s.\n",
               mgmt_udp_port, hostname.c_str());
}

template <class TTr>
Nexus<TTr>::~Nexus() {
  erpc_dprintf_noargs("eRPC Nexus: Destroying Nexus.\n");

  // Signal background and session management threads to kill themselves
  bg_kill_switch = true;
  sm_kill_switch = true;

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
  reg_hooks_arr[rpc_id] = hook;

  // Install background request and response submission lists
  for (size_t i = 0; i < num_bg_threads; i++) {
    BgThreadCtx &bg_ctx = bg_thread_ctx_arr[i];
    assert(bg_ctx.bg_thread_id == i);

    hook->bg_req_list_arr[i] = &bg_ctx.bg_req_list;  // Request
  }

  nexus_lock.unlock();
}

template <class TTr>
void Nexus<TTr>::unregister_hook(Hook *hook) {
  assert(hook != nullptr);

  uint8_t rpc_id = hook->rpc_id;
  assert(rpc_id <= kMaxRpcId);
  assert(reg_hooks_arr[rpc_id] == hook);
  erpc_dprintf("eRPC Nexus: Deregistering Rpc %u.\n", rpc_id);

  nexus_lock.lock();
  reg_hooks_arr[rpc_id] = nullptr;
  nexus_lock.unlock();
}

template <class TTr>
int Nexus<TTr>::register_req_func(uint8_t req_type, ReqFunc app_req_func) {
  // Create the basic issue message
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Nexus: Failed to register handlers for request type %u. Issue",
          req_type);

  // If any Rpc is already registered, the user cannot register new Ops
  if (!req_func_registration_allowed) {
    erpc_dprintf("%s: Registration not allowed anymore.\n", issue_msg);
    return -EPERM;
  }

  ReqFunc &arr_req_func = req_func_arr[req_type];

  // Check if this request type is already registered
  if (req_func_arr[req_type].is_registered()) {
    erpc_dprintf("%s: A handler for this request type already exists.\n",
                 issue_msg);
    return -EEXIST;
  }

  // Check if the application's Ops is valid
  if (app_req_func.req_func == nullptr) {
    erpc_dprintf("%s: Invalid handler.\n", issue_msg);
    return -EINVAL;
  }

  // If the request handler runs in the background, we must have bg threads
  if (app_req_func.is_background() && num_bg_threads == 0) {
    erpc_dprintf("%s: Background threads not available.\n", issue_msg);
    return -EPERM;
  }

  arr_req_func = app_req_func;
  return 0;
}

template <class TTr>
double Nexus<TTr>::get_freq_ghz() {
  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &start);
  uint64_t rdtsc_start = rdtsc();

  // Do not change this loop! The hardcoded value below depends on this loop
  // and prevents it from being optimized out.
  uint64_t sum = 5;
  for (uint64_t i = 0; i < 1000000; i++) {
    sum += i + (sum + i) * (i % sum);
  }

  if (sum != 13580802877818827968ull) {
    erpc_dprintf_noargs("eRPC: FATAL. Failed in rdtsc frequency measurement.");
    assert(false);
    exit(-1);
  }

  clock_gettime(CLOCK_REALTIME, &end);
  uint64_t clock_ns = (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000 +
                      (uint64_t)(end.tv_nsec - start.tv_nsec);
  uint64_t rdtsc_cycles = rdtsc() - rdtsc_start;

  double _freq_ghz = rdtsc_cycles / clock_ns;

  // Less than 500 MHz and greater than 5.0 GHz is abnormal
  if (_freq_ghz < 0.5 || _freq_ghz > 5.0) {
    erpc_dprintf("eRPC Nexus: FATAL. Abnormal CPU frequency %.4f GHz\n",
                 _freq_ghz);
    throw std::runtime_error("eRPC Nexus: get_freq_ghz() failed.");
  }

  return _freq_ghz;
}

template <class TTr>
std::string Nexus<TTr>::get_hostname() {
  char _hostname[kMaxHostnameLen];

  // Get the local hostname
  int ret = get_hostname(_hostname);
  if (ret == -1) {
    erpc_dprintf("eRPC Nexus: FATAL. gethostname failed. Error = %s.\n",
                 strerror(errno));
    throw std::runtime_error("eRPC Nexus: get_hostname() failed.");
  }

  return std::string(_hostname);
}

}  // End ERpc
