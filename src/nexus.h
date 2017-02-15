#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <signal.h>
#include <unistd.h>
#include <mutex>
#include <queue>
#include <vector>

#include "common.h"
#include "session.h"
using namespace std;

namespace ERpc {

class Nexus {
 public:
  /**
   * @brief The Nexus creation API exposed to the user. Creates the
   * one-per-process Nexus object. This sets @udp_drop_prob = 0.0.
   *
   * @param mgmt_udp_port The UDP port used by all Nexus-es in the cluster to
   * listen for session management packets.
   *
   * @throw \p runtime_error if Nexus creation fails.
   */
  Nexus(uint16_t mgmt_udp_port);

  /**
   * @brief Nexus creation API for UDP packet loss testing. Creates the
   * one-per-process Nexus object.
   *
   * @param mgmt_udp_port The UDP port used by all Nexus-es in the cluster to
   * listen for session management packets.
   *
   * @param udp_drop_prob The probability that a session management packet
   * will be dropped. This is useful for testing session management packet
   * retransmission.
   *
   * @throw \p runtime_error if Nexus creation fails.
   */
  Nexus(uint16_t mgmt_udp_port, double udp_drop_prob);

  ~Nexus();

  /**
   * @brief Check if a hook with app TID = \p app_tid exists in this Nexus.
   * If \p app_tid is derived from a hook, this function also checks if that
   * hook is already registered.
   *
   * The caller must not hold the Nexus lock before calling this.
   */
  bool app_tid_exists(uint8_t app_tid);

  /// Register a previously unregistered hook.
  void register_hook(SessionMgmtHook *hook);

  /// Unregister a previously registered hook.
  void unregister_hook(SessionMgmtHook *hook);

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

  // The Nexus object is shared among all Rpc objects, so we need to avoid
  // false sharing. Read-only members go first; other members come after
  // a cache line padding.
  char hostname[kMaxHostnameLen]; /* The local host's network hostname */
  double freq_ghz;
  const udp_config_t udp_config;
  int nexus_sock_fd; /* The file descriptor of the UDP socket */

  uint8_t pad[64];
  std::mutex nexus_lock; /* Held by Rpc threads to access Nexus */

  /* Hooks into session management objects registered by RPC objects */
  std::vector<SessionMgmtHook *> reg_hooks;

 private:
  /// Compute the frequency of rdtsc and set @freq_ghz
  void compute_freq_ghz();
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
