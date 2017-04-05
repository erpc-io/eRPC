#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <unistd.h>
#include "common.h"
#include "session.h"
#include "session_mgmt_types.h"
#include "small_rpc_optlevel.h"
#include "transport_impl/ib_transport.h"
#include "util/mt_list.h"

namespace ERpc {

// Forward declaration
template <typename T>
class Rpc;

template <class TTr>
class Nexus {
 public:
  static constexpr double kMaxUdpDropProb = .95;  ///< Max UDP packet drop prob

  enum class BgWorkItemType : bool { kReq, kResp };

  /// A work item submitted to a background thread
  class BgWorkItem {
   public:
    BgWorkItem(BgWorkItemType wi_type, uint8_t rpc_id, Rpc<TTr> *rpc,
               void *context, SSlot *sslot)
        : wi_type(wi_type),
          rpc_id(rpc_id),
          rpc(rpc),
          context(context),
          sslot(sslot) {}

    const BgWorkItemType wi_type;

    /// ID of the Rpc that submitted this request. Debug-only.
    const uint8_t rpc_id;
    Rpc<TTr> *rpc;
    void *context;  ///< The context to use for request handler
    SSlot *sslot;
  };

  /// Background thread context
  class BgThreadCtx {
   public:
    /// A switch used by the Nexus to turn off background threads
    volatile bool *bg_kill_switch;

    /// A pointer to the Nexus's request functions. Unlike Rpc threads that
    /// create a copy of the Nexus's request functions, background threads have
    /// a pointer. This is because background threads are launched before any
    /// request functions are registered.
    std::array<ReqFunc, kMaxReqTypes> *req_func_arr;

    size_t bg_thread_id;             ///< ID of the background thread
    MtList<BgWorkItem> bg_req_list;  ///< Background thread request list
  };

  /// A hook created by the per-thread Rpc, and shared with the per-process
  /// Nexus. All accesses to this hook must be done with @session_mgmt_mutex
  /// locked.
  class Hook {
   public:
    Hook(uint8_t rpc_id) : rpc_id(rpc_id) {}
    const uint8_t rpc_id;  ///< ID of the Rpc that created this hook
    MtList<SessionMgmtPkt *> sm_pkt_list;  ///< Session management packet list
    /// Background thread request lists
    MtList<BgWorkItem> *bg_req_list_arr[kMaxBgThreads] = {nullptr};
  };

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

  /// The function executed by background threads
  static void bg_thread_func(BgThreadCtx *bg_thread_ctx);

  /// The function executed by the session management thread
  static void sm_thread_func(volatile bool *sm_kill_switch,
                             volatile Hook **reg_hooks_arr,
                             std::mutex *nexus_lock,
                             const udp_config_t *udp_config);

  /**
   * @brief Check if a hook with Rpc ID = \p rpc_id exists in this Nexus. The
   * caller must not hold the Nexus lock before calling this.
   */
  bool rpc_id_exists(uint8_t rpc_id);

  /// Register a previously unregistered session management hook
  void register_hook(Hook *hook);

  /// Unregister a previously registered session management hook
  void unregister_hook(Hook *hook);

  void session_mgmt_handler();

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
  Hook *reg_hooks_arr[kMaxRpcId + 1] = {nullptr};  ///< Rpc-Nexus hooks

 private:
  // Session management thread
  volatile bool sm_kill_switch;  ///< A switch to turn off the SM thread
  std::thread sm_thread;         ///< The session management thread

  // Background threads
  volatile bool bg_kill_switch;  ///< A switch to turn off background threads
  std::thread bg_thread_arr[kMaxBgThreads];      ///< The background threads
  BgThreadCtx bg_thread_ctx_arr[kMaxBgThreads];  ///< Background thread context

  /// Return the frequency of rdtsc in GHz
  static double get_freq_ghz();

  /// Return the hostname of this machine
  static std::string get_hostname();
};

// Instantiate required Nexus classes so they get compiled for the linker
template class Nexus<IBTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H
