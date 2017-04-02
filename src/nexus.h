#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <signal.h>
#include <unistd.h>

#include "bg_thread.h"
#include "common.h"
#include "session.h"
#include "session_mgmt_types.h"
#include "small_rpc_optlevel.h"
#include "util/mt_list.h"

namespace ERpc {

/// A hook created by the per-thread Rpc, and shared with the per-process Nexus.
/// All accesses to this hook must be done with @session_mgmt_mutex locked.
class NexusHook {
 public:
  NexusHook(uint8_t app_tid) : app_tid(app_tid) {}
  const uint8_t app_tid;  ///< App TID of the RPC that created this hook
  MtList<SessionMgmtPkt *> sm_pkt_list;  ///< Session management packet list
  /// Background thread request lists
  MtList<BgWorkItem> *bg_req_list_arr[kMaxBgThreads] = {nullptr};
};

class Nexus {
  static constexpr double kMaxUdpDropProb = .95;  ///< Max UDP packet drop prob
 public:
  /**
   * @brief Create the one-per-process Nexus object.
   *
   * @param mgmt_udp_port The UDP port used by all Nexus-es in the cluster to
   * listen for session management packets
   *
   * @param num_bg_threads The number of background RPC request processing
   * threads to launch. This requires small_rpc_optlevel to not be
   * small_rpc_optlevel_extreme, which does not support background threads.
   *
   * @param udp_drop_prob The probability that a session management packet
   * will be dropped. This is useful for testing session management packet
   * retransmission.
   *
   * @throw runtime_error if Nexus creation fails.
   */
  Nexus(uint16_t mgmt_udp_port, size_t num_bg_threads = 0,
        double udp_drop_prob = 0.0);

  ~Nexus();

  /**
   * @brief Check if a hook with app TID = \p app_tid exists in this Nexus. The
   * caller must not hold the Nexus lock before calling this.
   */
  bool app_tid_exists(uint8_t app_tid);

  /// Register a previously unregistered session management hook
  void register_hook(NexusHook *hook);

  /// Unregister a previously registered session management hook
  void unregister_hook(NexusHook *hook);

  void install_sigio_handler();
  void session_mgnt_handler();

  /**
   * @brief Register application-defined request handler function. This
   * must be done before any Rpc registers a hook with the Nexus.
   *
   * @return 0 on success, negative errno on failure.
   */
  int register_req_func(uint8_t req_type, ReqFunc req_func);

  /**
   * @brief Copy the hostname of this machine to \p hostname. \p hostname must
   * have space for kMaxHostnameLen characters.
   *
   * @return 0 on success, -1 on error.
   */
  static int get_hostname(char *_hostname) {
    assert(_hostname != nullptr);

    int ret = gethostname(_hostname, kMaxHostnameLen);
    return ret;
  }
  /// Read-mostly members exposed to Rpc threads
  const udp_config_t udp_config;  ///< UDP port and packet drop probability
  const double freq_ghz;          ///< Rdtsc frequncy
  const std::string hostname;     ///< The local host
  const size_t num_bg_threads;    ///< Background threads to process Rpc reqs

  const uint8_t pad[64] = {0};

  /// The ground truth for registered request functions
  std::array<ReqFunc, kMaxReqTypes> req_func_arr;

  /// Request function registration is disallowed after any Rpc registers with
  /// the Nexus gets a copy of req_func_arr
  bool req_func_registration_allowed = true;

  /// Read-write members exposed to Rpc threads
  std::mutex nexus_lock;  ///< Lock for concurrent access to this Nexus
  NexusHook *reg_hooks_arr[kMaxAppTid + 1] = {nullptr};  ///< Rpc-Nexus hooks

 private:
  int sm_sock_fd;  ///< File descriptor of the session management UDP socket

  // Background threads
  volatile bool bg_kill_switch;  ///< A switch to turn off background threads
  std::thread bg_thread_arr[kMaxBgThreads];      ///< The background threads
  BgThreadCtx bg_thread_ctx_arr[kMaxBgThreads];  ///< Background thread context

  /// Return the frequency of rdtsc in GHz
  static double get_freq_ghz();

  /// Return the hostname of this machine
  static std::string get_hostname();
};

static Nexus *nexus_object;  // The one per-process Nexus object

/**
 * @brief The static signal handler, which executes the actual signal handler
 * with the one Nexus object.
 */
static void sigio_handler(int sig_num) {
  assert(sig_num == SIGIO);
  _unused(sig_num);
  nexus_object->session_mgnt_handler();
}

}  // End ERpc

#endif  // ERPC_RPC_H
