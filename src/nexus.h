#pragma once

#include <unordered_map>
#include "common.h"
#include "heartbeat_mgr.h"
#include "session.h"
#include "sm_types.h"
#include "util/logger.h"
#include "util/mt_queue.h"
#include "util/tls_registry.h"

namespace erpc {

// Forward declaration for friendship
template <typename T>
class Rpc;

/**
 * @brief A per-process library object used for initializing eRPC
 */
class Nexus {
  friend class Rpc<CTransport>;

  /**
   * @brief Initialize eRPC for this process
   *
   * @param local_uri A URI for this process formatted as hostname:udp_port.
   * This hostname and UDP port correspond to the "control" network interface of
   * the host, which eRPC uses for non-performance-critical session handshakes
   * and management traffic. This is different from the fast "datapath" network
   * interface that eRPC uses for performance-critical RPC traffic (see
   * Rpc::Rpc).
   *
   * The "control" network interface is often (a) a slow 1 Gbps NIC used for
   * SSH, (b) a non-accelerated NIC in a cloud. Generally, this hostname should
   * be reachable from other servers using eRPC via ping.
   *
   * This UDP port is used for listening to management packets, and it must
   * be in [#kBaseSmUdpPort, #kBaseSmUdpPort + #kMaxNumERpcProcesses).
   *
   * @param numa_node The NUMA node used by eRPC for this process
   *
   * @param num_bg_threads The number of background RPC request processing
   * threads to launch.
   *
   * @throw runtime_error if Nexus creation fails.
   */
 public:
  Nexus(std::string local_uri, size_t numa_node = 0, size_t num_bg_threads = 0);

  ~Nexus();

  /**
   * @brief Register application-defined request handler function. This
   * must be done before any Rpc registers a hook with the Nexus.
   *
   * @return 0 on success, negative errno on failure.
   */
  int register_req_func(uint8_t req_type, erpc_req_func_t req_func,
                        ReqFuncType req_func_type = ReqFuncType::kForeground);

 private:
  enum class BgWorkItemType : bool { kReq, kResp };

  /// A work item submitted to a background thread
  class BgWorkItem {
   public:
    BgWorkItem() {}

    static inline BgWorkItem make_req_item(void *context, SSlot *sslot) {
      BgWorkItem ret;
      ret.wi_type_ = BgWorkItemType::kReq;
      ret.context_ = context;
      ret.sslot_ = sslot;
      return ret;
    }

    static inline BgWorkItem make_resp_item(void *context,
                                            erpc_cont_func_t cont_func,
                                            void *tag) {
      BgWorkItem ret;
      ret.wi_type_ = BgWorkItemType::kResp;
      ret.context_ = context;
      ret.cont_func_ = cont_func;
      ret.tag_ = tag;
      return ret;
    }

    BgWorkItemType wi_type_;
    void *context_;  ///< The Rpc's context

    // Fields for request handlers. For request handlers, we still have
    // ownership of the request slot, so we can hold it until enqueue_response.
    SSlot *sslot_;

    // Fields for continuations. For continuations, we have lost ownership of
    // the request slot, so the work item contains all needed info by value.
    erpc_cont_func_t cont_func_;
    void *tag_;

    bool is_req() const { return wi_type_ == BgWorkItemType::kReq; }
  };

  /// A hook created by an Rpc thread, and shared with the Nexus
  class Hook {
   public:
    uint8_t rpc_id_;  ///< ID of the Rpc that created this hook

    /// Background thread request queues, installed by the Nexus
    MtQueue<BgWorkItem> *bg_req_queue_arr_[kMaxBgThreads] = {nullptr};

    /// The Rpc thread's session management RX queue, installed by the Rpc.
    /// Work items from the SM thread for this Rpc are queued here.
    MtQueue<SmWorkItem> sm_rx_queue_;
  };

  /// Check if a hook with for rpc_id exists in this Nexus. The caller must not
  /// hold the Nexus lock before calling this.
  bool rpc_id_exists(uint8_t rpc_id);

  /// Register a previously unregistered session management hook
  void register_hook(Hook *hook);

  /// Unregister a previously registered session management hook
  void unregister_hook(Hook *hook);

  /// Background thread context
  class BgThreadCtx {
   public:
    volatile bool *kill_switch_;  ///< The Nexus's kill switch

    /// The Nexus's request functions array. Unlike Rpc threads that create a
    /// copy of the Nexus's request functions, background threads have a
    /// pointer. This is because background threads are launched before request
    /// functions are registered.
    std::array<ReqFunc, kReqTypeArraySize> *req_func_arr_;

    TlsRegistry *tls_registry_;          ///< The Nexus's thread-local registry
    size_t bg_thread_index_;             ///< Index of this background thread
    MtQueue<BgWorkItem> *bg_req_queue_;  ///< Background thread request queue
  };

  /// Session management thread context
  class SmThreadCtx {
   public:
    // Installed by the Nexus
    std::string hostname_;  ///< User-provided hostname of this node
    uint16_t sm_udp_port_;  ///< The Nexus's session management port
    double freq_ghz_;       ///< RDTSC frequency

    /// The kill switch installed by the Nexus. When this becomes true, the SM
    /// thread should terminate itself.
    volatile bool *kill_switch_;

    HeartbeatMgr *heartbeat_mgr_;    ///< The Nexus's heartbeat manager
    volatile Hook **reg_hooks_arr_;  ///< The Nexus's hooks array
    std::mutex *reg_hooks_lock_;
  };

  /// The background thread
  static void bg_thread_func(BgThreadCtx ctx);

  /// The session management thread
  static void sm_thread_func(SmThreadCtx ctx);

  /// Read-mostly members exposed to Rpc threads
  const double freq_ghz_;        ///< TSC frequncy
  const std::string hostname_;   ///< The local host
  const uint16_t sm_udp_port_;   ///< UDP port for session management
  const size_t numa_node_;       ///< The NUMA node for this process
  const size_t num_bg_threads_;  ///< Background threads to process Rpc reqs
  TlsRegistry tls_registry_;     ///< A thread-local registry

  /// The ground truth for registered request functions
  std::array<ReqFunc, kReqTypeArraySize> req_func_arr_;
  const uint8_t pad_[64] = {0};  ///< Separate read-write members from read-only

  /// Request function registration is disallowed after any Rpc registers with
  /// the Nexus and gets a copy of req_func_arr
  bool req_func_registration_allowed_ = true;

  /// Rpc-Nexus hooks. All non-null hooks are valid.
  Hook *reg_hooks_arr_[kMaxRpcId + 1] = {nullptr};
  std::mutex reg_hooks_lock_;  /// Lock for concurrent access to the hooks array

  HeartbeatMgr heartbeat_mgr_;  ///< The heartbeat manager
  volatile bool kill_switch_;   ///< Used to turn off SM and background threads

  std::thread sm_thread_;  ///< The session management thread
  MtQueue<BgWorkItem> bg_req_queue_[kMaxBgThreads];  ///< Background req queues
  std::thread bg_thread_arr_[kMaxBgThreads];  ///< Background thread context
};
}  // namespace erpc
