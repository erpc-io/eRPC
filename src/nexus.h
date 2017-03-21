#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <signal.h>
#include <unistd.h>
#include <mutex>
#include <queue>
#include <vector>

#include "common.h"
#include "session.h"
#include "session_mgmt_types.h"
using namespace std;

namespace ERpc {

/// A work item submitted to a background thread. Also a work completion.
struct bg_work_item_t {
  uint8_t app_tid;  ///< TID of the Rpc that submitted this request
  Session *session;
  Session::sslot_t *sslot;
};

/// A hook created by the per-thread Rpc, and shared with the per-process Nexus.
/// All accesses to this hook must be done with @session_mgmt_mutex locked.
class NexusHook {
 public:
  NexusHook(uint8_t app_tid) : app_tid(app_tid) {
    sm_pkt_counter = 0;
    bg_resp_counter = 0;
  }

  const uint8_t app_tid;  ///< App TID of the RPC that created this hook

  ///@{ Session management packet list
  std::mutex sm_pkt_list_lock;
  volatile size_t sm_pkt_counter;
  std::vector<SessionMgmtPkt *> sm_pkt_list;
  ///@}

  ///@{ Background thread response list
  std::mutex bg_resp_list_lock;
  volatile size_t bg_resp_counter;
  std::vector<bg_work_item_t *> bg_resp_list;
  ///@}
};

class Nexus {
  static constexpr size_t kMaxBgThreads = 8;  ///< Maximum background threads
  static constexpr double kMaxUdpDropProb = .95;  ///< Max UDP packet drop prob
 public:
  /**
   * @brief Create the one-per-process Nexus object.
   *
   * @param mgmt_udp_port The UDP port used by all Nexus-es in the cluster to
   * listen for session management packets
   *
   * @param num_bg_threads The number of background RPC request processing
   * threads to launch
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

  /// Read-write members exposed to Rpc threads
  std::mutex nexus_lock;  ///< Lock for concurrent access to this Nexus
  NexusHook *reg_hooks_arr[kMaxAppTid + 1] = {nullptr};  ///< Rpc-Nexus hooks

 private:
  int sm_sock_fd;  ///< File descriptor of the session management UDP socket

  /// Return the frequency of rdtsc in GHz
  static double get_freq_ghz();

  /// Return the hostname of this machine
  static std::string get_hostname();
};

static Nexus *nexus_object; /* The one per-process Nexus object */

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
