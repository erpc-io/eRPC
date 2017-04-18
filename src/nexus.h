#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <enet/enet.h>
#include <unistd.h>
#include <unordered_map>
#include "common.h"
#include "session.h"
#include "session_mgmt_types.h"
#include "small_rpc_optlevel.h"
#include "transport_impl/ib_transport.h"
#include "util/mt_list.h"
#include "util/tls_registry.h"

namespace ERpc {

// Forward declaration
template <typename T>
class Rpc;

template <class TTr>
class Nexus {
 public:
  static constexpr double kMaxUdpDropProb = .95;  ///< Max UDP packet drop prob

  enum class WorkItemType : bool { kReq, kResp };

  /// A work item submitted to a background thread
  class BgWorkItem {
   public:
    BgWorkItem(WorkItemType wi_type, Rpc<TTr> *rpc, void *context, SSlot *sslot)
        : wi_type(wi_type), rpc(rpc), context(context), sslot(sslot) {}

    const WorkItemType wi_type;
    Rpc<TTr> *rpc;  ///< The Rpc object that submitted this work item
    void *context;  ///< The context to use for request handler
    SSlot *sslot;

    bool is_req() const { return wi_type == WorkItemType::kReq; }
  };

  /// Background thread context
  class BgThreadCtx {
   public:
    volatile bool *kill_switch;  ///< The Nexus's kill switch

    /// A pointer to the Nexus's request functions. Unlike Rpc threads that
    /// create a copy of the Nexus's request functions, background threads have
    /// a pointer. This is because background threads are launched before any
    /// request functions are registered.
    std::array<ReqFunc, kMaxReqTypes> *req_func_arr;

    TlsRegistry *tls_registry;       ///< The Nexus's thread-local registry
    size_t bg_thread_index;          ///< Index of this background thread
    MtList<BgWorkItem> bg_req_list;  ///< Background thread request list
  };

  // Session management thread definitions
  class Hook;  // Forward declaration

  /// A work item exchanged between an Rpc thread and an SM thread
  class SmWorkItem {
   public:
    SmWorkItem(uint8_t rpc_id, SessionMgmtPkt *sm_pkt, ENetPeer *peer)
        : rpc_id(rpc_id), sm_pkt(sm_pkt), peer(peer) {
      assert(sm_pkt != nullptr);
    };

    const uint8_t rpc_id;    ///< The local Rpc ID
    SessionMgmtPkt *sm_pkt;  ///< The SM packet for this work item
    ENetPeer *peer;
  };

  /// Session management thread context
  class SmThreadCtx {
   public:
    // Installed by the Nexus
    uint16_t mgmt_udp_port;         ///< The Nexus's session management port
    volatile bool *kill_switch;     ///< The Nexus's kill switch
    volatile Hook **reg_hooks_arr;  ///< A pointer to the Nexus's hooks array
    std::mutex *nexus_lock;
    MtList<SmWorkItem> sm_tx_list;  ///< SM packets to transmit

    // Used internally by the SM thread
    ENetHost *enet_host;

    // Mappings maintained for client sessions only
    std::unordered_map<std::string, ENetPeer *> name_map;
    std::unordered_map<uint32_t, std::string> ip_map;
  };

  /// Peer metadata maintained by client peers
  class SmPeerData {
   public:
    std::string rem_hostname;
    bool connected;
    std::vector<SmWorkItem> work_item_vec;
  };

  /// A hook created by an Rpc thread, and shared with the Nexus
  class Hook {
   public:
    uint8_t rpc_id;  ///< ID of the Rpc that created this hook

    /// Background thread request lists, installed by the Nexus
    MtList<BgWorkItem> *bg_req_list_arr[kMaxBgThreads] = {nullptr};

    /// Session management thread's session management TX list, installed by
    /// Nexus. This is used by Rpc threads to submit packets to the SM thread.
    MtList<SmWorkItem> *sm_tx_list = nullptr;

    /// The Rpc thread's session management RX list, installed by the Rpc.
    /// Packets received by the SM thread for this Rpc are queued here.
    MtList<SmWorkItem> sm_rx_list;
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
  static void bg_thread_func(BgThreadCtx *ctx);

  /// The function executed by the session management thread
  static void sm_thread_func(SmThreadCtx *ctx);

  /// Transmit a work item and free its SM packet memory
  static void sm_tx_work_item_and_free(SmWorkItem *wi);

  /// Receive session management packets and enqueue them to Rpc threads. This
  /// blocks for up to \p kSmThreadEventLoopMs, lowering CPU use.
  static void sm_thread_rx(SmThreadCtx *ctx);

  /// Transmit session management packets enqueued by Rpc threads
  static void sm_thread_tx(SmThreadCtx *ctx);

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
  const double freq_ghz;        ///< Rdtsc frequncy
  const std::string hostname;   ///< The local host
  const size_t num_bg_threads;  ///< Background threads to process Rpc reqs

  const uint8_t pad[64] = {0};

  TlsRegistry tls_registry;  ///< A thread-local registry

  /// The ground truth for registered request functions
  std::array<ReqFunc, kMaxReqTypes> req_func_arr;

  /// Request function registration is disallowed after any Rpc registers with
  /// the Nexus gets a copy of req_func_arr
  bool req_func_registration_allowed = true;

  /// Read-write members exposed to Rpc threads
  std::mutex nexus_lock;  ///< Lock for concurrent access to this Nexus
  Hook *reg_hooks_arr[kMaxRpcId + 1] = {nullptr};  ///< Rpc-Nexus hooks

 private:
  volatile bool kill_switch;  ///< Used to turn of SM and background threads

  // Session management thread
  SmThreadCtx sm_thread_ctx;  ///< Session management thread context
  std::thread sm_thread;      ///< The session management thread

  // Background threads
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
