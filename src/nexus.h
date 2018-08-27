#pragma once

#include <unistd.h>
#include <unordered_map>
#include "common.h"
#include "session.h"
#include "sm_types.h"
#include "util/logger.h"
#include "util/mt_queue.h"
#include "util/tls_registry.h"

namespace erpc {

/// A per-process object that manages the background threads, the session
/// management thread, and request handler registration.
class Nexus {
 public:
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

    /// The Rpc thread's session management RX queue, installed by the Rpc.
    /// Packets received by the SM thread for this Rpc are queued here.
    MtQueue<SmPkt> sm_rx_queue;
  };

  /**
   * @brief Create the one-per-process Nexus object.
   *
   * @param hostname The local URI formatted as hostname:udp_port
   * @param numa_node NUMA node for this eRPC process. Only one eRPC process
   * may run per NUMA node.
   * @param num_bg_threads The number of background RPC request processing
   * threads to launch.
   *
   * @throw runtime_error if Nexus creation fails.
   */
  Nexus(std::string local_uri, size_t numa_node, size_t num_bg_threads);

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
  int register_req_func(uint8_t req_type, erpc_req_func_t req_func,
                        ReqFuncType req_func_type = ReqFuncType::kForeground);

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
    uint16_t sm_udp_port;           ///< The Nexus's session management port
    volatile bool *kill_switch;     ///< The Nexus's kill switch
    volatile Hook **reg_hooks_arr;  ///< The Nexus's hooks array
    std::mutex *nexus_lock;
  };

  /// The background thread
  static void bg_thread_func(BgThreadCtx ctx);

  /// The session management thread
  static void sm_thread_func(SmThreadCtx ctx);

 public:
  /// Read-mostly members exposed to Rpc threads
  const double freq_ghz;        ///< TSC frequncy
  const std::string hostname;   ///< The local host
  const uint16_t sm_udp_port;   ///< UDP port for session management
  const size_t numa_node;       ///< The NUMA node for this process
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

  std::thread sm_thread;  ///< The session management thread
  MtQueue<BgWorkItem> bg_req_queue[kMaxBgThreads];  ///< Background req queues
  std::thread bg_thread_arr[kMaxBgThreads];  ///< Background thread context
};
}  // namespace erpc
