#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <enet/enet.h>
#include <unistd.h>
#include <unordered_map>
#include "common.h"
#include "session.h"
#include "sm_types.h"
#include "transport_impl/ib_transport.h"
#include "util/mt_queue.h"
#include "util/tls_registry.h"

namespace erpc {

/// A work item exchanged between an Rpc thread and an SM thread. This does
/// not have any Nexus-related members, so it's outside the Nexus class.
class SmWorkItem {
  enum class Reset { kFalse, kTrue };

 public:
  SmWorkItem(uint8_t rpc_id, SmPkt sm_pkt)
      : reset(Reset::kFalse), rpc_id(rpc_id), sm_pkt(sm_pkt) {}

  SmWorkItem(std::string reset_rem_hostname)
      : reset(Reset::kTrue),
        rpc_id(kInvalidRpcId),
        reset_rem_hostname(reset_rem_hostname) {}

  bool is_reset() const { return reset == Reset::kTrue; }

  const Reset reset;     ///< Is this work item a reset?
  const uint8_t rpc_id;  ///< The local Rpc ID, invalid for reset work items

  SmPkt sm_pkt;  ///< The session management packet, for non-reset work items

  /// The remote hostname to reset, valid for reset work items
  std::string reset_rem_hostname;
};

class Nexus {
 public:
  static constexpr size_t kNexusSmThreadCore = 15;  /// CPU core for SM thread
  enum class BgWorkItemType : bool { kReq, kResp };

  /// A work item submitted to a background thread
  class BgWorkItem {
   public:
    BgWorkItem(BgWorkItemType wi_type, uint8_t rpc_id, void *context,
               SSlot *sslot)
        : wi_type(wi_type), rpc_id(rpc_id), context(context), sslot(sslot) {}

    const BgWorkItemType wi_type;
    const uint8_t rpc_id;  ///< The Rpc ID that submitted this work item
    void *context;         ///< The context to use for request handler
    SSlot *sslot;

    bool is_req() const { return wi_type == BgWorkItemType::kReq; }
  };

  /// A hook created by an Rpc thread, and shared with the Nexus
  class Hook {
   public:
    uint8_t rpc_id;  ///< ID of the Rpc that created this hook

    /// Background thread request queues, installed by the Nexus
    MtQueue<BgWorkItem> *bg_req_queue_arr[kMaxBgThreads] = {nullptr};

    /// The session management TX queue, installed by Nexus. This is used by Rpc
    /// threads to submit packets to the SM thread.
    MtQueue<SmWorkItem> *sm_tx_queue = nullptr;

    /// The Rpc thread's session management RX queue, installed by the Rpc.
    /// Packets received by the SM thread for this Rpc are queued here.
    MtQueue<SmWorkItem> sm_rx_queue;
  };

  /**
   * @brief Create the one-per-process Nexus object.
   *
   * @param hostname The IP host name of this host
   *
   * @param mgmt_udp_port The UDP port used by all Nexus-es in the cluster to
   * listen for session management packets
   *
   * @param num_bg_threads The number of background RPC request processing
   * threads to launch. This requires small_rpc_optlevel to not be
   * small_rpc_optlevel_extreme, which does not support background threads.
   *
   * @throw runtime_error if Nexus creation fails.
   */
  Nexus(std::string hostname, uint16_t mgmt_udp_port,
        size_t num_bg_threads = 0);

  ~Nexus();

  /// Check if a hook with for rpc_id exists in this Nexus. The caller must not
  /// hold the Nexus lock before calling this.
  bool rpc_id_exists(uint8_t rpc_id);

  /// Register a previously unregistered session management hook
  void register_hook(Hook *hook);

  /// Unregister a previously registered session management hook
  void unregister_hook(Hook *hook);

  /**
   * @brief Register application-defined request handler function. This
   * must be done before any Rpc registers a hook with the Nexus.
   *
   * @return 0 on success, negative errno on failure.
   */
  int register_req_func(uint8_t req_type, ReqFunc req_func);

 private:
  /// Background thread context
  class BgThreadCtx {
   public:
    volatile bool *kill_switch;  ///< The Nexus's kill switch

    /// The Nexus's request functions array. Unlike Rpc threads that create a
    /// copy of the Nexus's request functions, background threads have a
    /// pointer. This is because background threads are launched before request
    /// functions are registered.
    std::array<ReqFunc, kReqTypeArraySize> *req_func_arr;

    TlsRegistry *tls_registry;          ///< The Nexus's thread-local registry
    size_t bg_thread_index;             ///< Index of this background thread
    MtQueue<BgWorkItem> *bg_req_queue;  ///< Background thread request queue
  };

  /// Session management thread context
  class SmThreadCtx {
   public:
    // Installed by the Nexus
    std::string hostname;           ///< User-provided hostname of this node
    uint16_t mgmt_udp_port;         ///< The Nexus's session management port
    volatile bool *kill_switch;     ///< The Nexus's kill switch
    volatile Hook **reg_hooks_arr;  ///< The Nexus's hooks array
    std::mutex *nexus_lock;
    MtQueue<SmWorkItem> *sm_tx_queue;  ///< SM packets to transmit

    // Created internally by the SM thread
    ENetHost *enet_host;

    /// Map remote hostnames to client-mode peers
    std::unordered_map<std::string, ENetPeer *> client_map;

    /// Map remote hostnames to server-mode peers
    std::unordered_map<std::string, ENetPeer *> server_map;
  };

  enum class SmENetPeerMode { kServer, kClient };

  /// ENet peer data
  class SmENetPeerData {
   public:
    SmENetPeerMode peer_mode;
    std::string rem_hostname;

    struct {
      /// True while we have a connection to the server peer. We never reconnect
      /// after disconnecting, so this is set to true only once.
      bool connected = false;

      /// Work items to transmit when the client's connection is established
      std::vector<SmWorkItem> tx_queue;
    } client;

    struct {
      /// Server-mode peers are created on an ENet connect event, at which time
      /// we don't have the remote hostname. This records if we've installed the
      /// remote hostname and server map entry.
      bool initialized = false;
    } server;

    SmENetPeerData(SmENetPeerMode peer_mode) : peer_mode(peer_mode) {}
    bool is_server() const { return peer_mode == SmENetPeerMode::kServer; }
    bool is_client() const { return peer_mode == SmENetPeerMode::kClient; }
  };

  /// Measure RDTSC frequency. This is expensive and only done once per process.
  double measure_rdtsc_freq();

  /// The function executed by background threads
  static void bg_thread_func(BgThreadCtx ctx);

  //
  // Session management thread functions (nexus_sm_thread.cc)
  //

  /// The thread function executed by the session management thread
  static void sm_thread_func(SmThreadCtx ctx);

  /// Handle an ENet connect event for a server-mode peer
  static void sm_thread_on_enet_connect_server(SmThreadCtx &ctx,
                                               ENetEvent &event);

  /// Handle an ENet connect event for a client-mode peer
  static void sm_thread_on_enet_connect_client(SmThreadCtx &ctx,
                                               ENetEvent &event);

  /// Broadcast a reset work item to all registered Rpcs
  static void sm_thread_broadcast_reset(SmThreadCtx &ctx,
                                        std::string rem_hostname);

  /// Handle an ENet disconnect event for a server-mode peer
  static void sm_thread_on_enet_disconnect_server(SmThreadCtx &ctx,
                                                  ENetEvent &event);

  /// Handle an ENet disconnect event for a client-mode peer
  static void sm_thread_on_enet_disconnect_client(SmThreadCtx &ctx,
                                                  ENetEvent &event);

  /// Retrieve a session management packet from this ENet event, and free the
  /// ENet-allocated memory
  static SmPkt sm_thread_pull_sm_pkt(ENetEvent &event);

  /// Handle an ENet receive event for a server-mode peer
  static void sm_thread_on_enet_receive_server(SmThreadCtx &ctx,
                                               ENetEvent &event);

  /// Handle an ENet receive event for a client-mode peer
  static void sm_thread_on_enet_receive_client(SmThreadCtx &ctx,
                                               ENetEvent &event);

  /// Receive session management packets and enqueue them to Rpc threads. This
  /// blocks for up to \p kSmThreadEventLoopMs, lowering CPU use.
  static void sm_thread_rx(SmThreadCtx &ctx);

  /// Process request work items enqueued by Rpc threads
  static void sm_thread_process_tx_queue_req(SmThreadCtx &ctx,
                                             const SmWorkItem &wi);

  /// Process response work items enqueued by Rpc threads
  static void sm_thread_process_tx_queue_resp(SmThreadCtx &ctx,
                                              const SmWorkItem &wi);

  /// Process work items enqueued by Rpc threads
  static void sm_thread_process_tx_queue(SmThreadCtx &ctx);

  /// Transmit one work item over a connected ENet peer
  static void sm_thread_enet_send_one(const SmWorkItem &wi, ENetPeer *epeer);

 public:
  /// Read-mostly members exposed to Rpc threads
  const double freq_ghz;        ///< TSC frequncy
  const std::string hostname;   ///< The local host
  const size_t num_bg_threads;  ///< Background threads to process Rpc reqs
  TlsRegistry tls_registry;     ///< A thread-local registry

  /// The ground truth for registered request functions
  std::array<ReqFunc, kReqTypeArraySize> req_func_arr;
  const uint8_t pad[64] = {0};  ///< Separate read-write members from read-only

 private:
  /// Request function registration is disallowed after any Rpc registers with
  /// the Nexus and gets a copy of req_func_arr
  bool req_func_registration_allowed = true;

  std::mutex nexus_lock;  ///< Lock for concurrent access to this Nexus

  /// Rpc-Nexus hooks. Non-null hooks are valid.
  Hook *reg_hooks_arr[kMaxRpcId + 1] = {nullptr};

  volatile bool kill_switch;  ///< Used to turn off SM and background threads

  // Session management thread
  MtQueue<SmWorkItem> sm_tx_queue;  ///< SM packet submission queue
  std::thread sm_thread;            ///< The session management thread

  // Background threads
  MtQueue<BgWorkItem> bg_req_queue[kMaxBgThreads];  ///< Background req queues
  std::thread bg_thread_arr[kMaxBgThreads];  ///< Background thread context
};
}  // End erpc

#endif  // ERPC_RPC_H
