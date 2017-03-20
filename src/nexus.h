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

/// A hook created by the per-thread Rpc, and shared with the per-process Nexus.
/// All accesses to this hook must be done with @session_mgmt_mutex locked.
struct nexus_hook_t {
  uint8_t app_tid;  ///< App TID of the RPC that created this hook
  std::mutex _lock;
  volatile size_t session_mgmt_pkt_counter;  ///< Number of session mgmt events
  std::vector<SessionMgmtPkt *> session_mgmt_pkt_list;  ///< List of mgmt pkts

  void lock() { _lock.lock(); }
  void unlock() { _lock.unlock(); }
};

class Nexus {
  static constexpr size_t kMaxBgThreads = 8;  ///< Maximum background threads
  static constexpr double kMaxUdpDropProb = .95;  ///< Max UDP packet drop prob
 public:
  /**
   * @brief Nexus creation API for UDP packet loss testing. Creates the
   * one-per-process Nexus object.
   *
   * @param mgmt_udp_port The UDP port used by all Nexus-es in the cluster to
   * listen for session management packets.
   *
   * @param num_bg_threads The number of background RPC request processing
   * threads to launch.
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
  void register_hook(nexus_hook_t *hook);

  /// Unregister a previously registered session management hook
  void unregister_hook(nexus_hook_t *hook);

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

  /// Number of background threads that process RPC requests. This can only
  /// increase over time.
  volatile size_t num_bg_threads;

  const uint8_t pad[64] = {0};

  /// Read-write members exposed to Rpc threads
  std::mutex nexus_lock;  ///< Lock for concurrent access to this Nexus
  std::vector<nexus_hook_t *> reg_hooks;  ///< Hooks registered by Rpcs

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
