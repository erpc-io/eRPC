#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include <random>
#include <vector>
#include "common.h"
#include "nexus.h"
#include "session.h"
#include "transport.h"
#include "transport_impl/ib_transport.h"
#include "util/buffer.h"
#include "util/huge_alloc.h"
#include "util/rand.h"

namespace ERpc {

// Per-thread RPC object
template <class Transport_>
class Rpc {
 public:
  /// Max request or response size, excluding packet headers
  static const size_t kMaxMsgSize = MB(8);
  static_assert((1ull << kMsgSizeBits) >= kMaxMsgSize, "");
  static_assert((1ull << kPktNumBits) * Transport_::kMaxDataPerPkt >=
                    kMaxMsgSize,
                "");

  /// Initial capacity of the hugepage allocator
  static const size_t kInitialHugeAllocSize = (128 * MB(1));

  /// Max number of unexpected *packets* kept outstanding by this Rpc
  static const size_t kRpcPktWindow = 20;

  /// Error codes returned by the Rpc datapath
  enum class RpcDatapathErrCode : int {
    kInvalidSessionArg,
    kInvalidMsgBufferArg,
    kInvalidMsgSizeArg,
    kNoSessionMsgSlots
  };

  static std::string rpc_err_code_str(RpcDatapathErrCode e) {
    switch (e) {
      case RpcDatapathErrCode::kInvalidSessionArg:
        return std::string("[Invalid session argument]");
      case RpcDatapathErrCode::kInvalidMsgBufferArg:
        return std::string("[Invalid MsgBuffer argument]");
      case RpcDatapathErrCode::kInvalidMsgSizeArg:
        return std::string("[Invalid message size argument]");
      case RpcDatapathErrCode::kNoSessionMsgSlots:
        return std::string("[No session message slots]");
    };

    assert(false);
    exit(-1);
    return std::string("");
  }

  // rpc.cc

  /**
   * @brief Construct the Rpc object
   * @throw runtime_error if construction fails
   */
  Rpc(Nexus *nexus, void *context, uint8_t app_tid,
      session_mgmt_handler_t session_mgmt_handler, uint8_t phy_port = 0,
      size_t numa_node = 0);

  ~Rpc();

  /**
   * @brief Create a hugepage-backed MsgBuffer for the eRPC user. The returned
   * MsgBuffer's \p buf is surrounded by packet headers that the user must not
   * modify.
   *
   * @param size Non-header bytes in the returned MsgBuffer (equal to the
   * \p size field of the returned MsgBuffer)
   *
   * @return \p The allocated MsgBuffer. The MsgBuffer is invalid (i.e., its
   * \p buf is NULL) if we ran out of memory.
   *
   * @throw runtime_error if \p size is too large for the allocator, or if
   * hugepage reservation failure is catastrophic. An exception is *not* thrown
   * if allocation fails simply because we ran out of memory.
   */
  inline MsgBuffer alloc_msg_buffer(size_t data_size) {
    size_t num_pkts;

    /* Avoid division for small packets */
    if (small_msg_likely(data_size <= Transport_::kMaxDataPerPkt)) {
      num_pkts = 1;
    } else {
      num_pkts = (data_size + (Transport_::kMaxDataPerPkt - 1)) /
                 Transport_::kMaxDataPerPkt;
    }

    /* This multiplication is a shift */
    static_assert(is_power_of_two(sizeof(pkthdr_t)), "");
    Buffer buffer =
        huge_alloc->alloc(data_size + (num_pkts * sizeof(pkthdr_t)));
    if (unlikely(buffer.buf == nullptr)) {
      return MsgBuffer(Buffer::get_invalid_buffer(), 0, 0);
    }

    MsgBuffer msg_buffer(buffer, data_size, num_pkts);

    /* Set the packet header magic in the 0th header */
    pkthdr_t *pkthdr = msg_buffer.get_pkthdr_0();
    pkthdr->magic = kPktHdrMagic;
    return msg_buffer;
  }

  /// Free a MsgBuffer allocated using alloc_msg_buffer()
  inline void free_msg_buffer(MsgBuffer msg_buffer) {
    assert(msg_buffer.buf != nullptr);
    assert(msg_buffer.check_pkthdr_0());

    huge_alloc->free_buf(msg_buffer.buffer);
  }

  // rpc_sm_api.cc

  /**
   * @brief Create a Session and initiate session connection.
   *
   * @return A pointer to the created session if creation succeeds and the
   * connect request is sent, NULL otherwise.
   *
   * A callback of type \p kConnected or \p kConnectFailed will be invoked if
   * this call is successful.
   */
  Session *create_session(const char *_rem_hostname, uint8_t rem_app_tid,
                          uint8_t rem_phy_port = 0);

  /**
   * @brief Disconnect and destroy a client session. \p session should not
   * be used by the application after this function is called.
   *
   * @param session A session that was returned by create_session().
   *
   * @return True if (a) the session disconnect packet was sent, and the
   * disconnect callback will be invoked later, or if (b) there was no need for
   * a disconnect packet since the session is in an error state. In the latter
   * case, the callback is invoked before this function returns.
   * The possible callback types are \p kDisconnected and \p kDisconnectFailed.
   *
   * False if the session cannot be disconnected right now since connection
   * establishment is in progress , or if the \p session argument is invalid.
   */
  bool destroy_session(Session *session);

  /// Return the number of active server or client sessions.
  size_t num_active_sessions();

  // rpc_datapath.cc

  /**
   * @brief Try to enqueue a request MsgBuffer for transmission. This function
   * initializes all the packet headers of the MsgBuffer.
   *
   * @param session The client session to send the request on
   * @param req_type The type of the request
   * @param msg_buffer The MsgBuffer containing the request. If this call
   * succeeds, eRPC owns \p msg_buffer until the request completes (i.e., the
   * callback is invoked).
   *
   * @return 0 on success, i.e., if the request was queued. An error code is
   * returned if the request cannot be queued.
   */
  int send_request(Session *session, uint8_t req_type, MsgBuffer *msg_buffer);

  // rpc_ev_loop.cc

  /// Run one iteration of the event loop
  void run_event_loop_one();

  /// Run the event loop forever
  inline void run_event_loop() {
    while (true) {
      run_event_loop_one();
    }
  }

  /// Run the event loop for \p timeout_ms milliseconds
  inline void run_event_loop_timeout(size_t timeout_ms) {
    uint64_t start_tsc = rdtsc();

    while (true) {
      run_event_loop_one();

      double elapsed_ms = to_sec(rdtsc() - start_tsc, nexus->freq_ghz) * 1000;
      if (elapsed_ms > timeout_ms) {
        return;
      }
    }
  }

 private:
  // rpc.cc

  /// Return the hostname and app TID of this Rpc.
  std::string get_name();

  /// Process all session management events in the queue and free them.
  /// The handlers for individual request/response types should not free
  /// packets.
  void handle_session_management();

  /// Free session resources and mark it as NULL in the session vector
  void bury_session(Session *session);

  // rpc_connect_handlers.cc
  void handle_session_connect_req(SessionMgmtPkt *pkt);
  void handle_session_connect_resp(SessionMgmtPkt *pkt);

  // rpc_disconnect_handlers.cc
  void handle_session_disconnect_req(SessionMgmtPkt *pkt);
  void handle_session_disconnect_resp(SessionMgmtPkt *pkt);

  // rpc_sm_retry.cc

  /// Send a (possibly retried) connect request for this session
  void send_connect_req_one(Session *session);

  /// Send a (possibly retried) disconnect request for this session
  void send_disconnect_req_one(Session *session);

  /// Add this session to the session managment retry queue
  void mgmt_retry_queue_add(Session *session);

  /// Remove this session from the session managment retry queue
  void mgmt_retry_queue_remove(Session *session);

  /// Check if the session managment retry queue contains this session
  bool mgmt_retry_queue_contains(Session *session);

  /// Retry in-flight session management requests whose timeout has expired
  void mgmt_retry();

  // rpc_ev_loop.cc

  /// Try to transmit packets for Sessions in the \p datapath_work_queue.
  /// Sessions for which all packets can be sent are removed from the queue.
  void process_datapath_work_queue();

  // Constructor args
  Nexus *nexus;
  void *context;  ///< The application context
  uint8_t app_tid;
  session_mgmt_handler_t session_mgmt_handler;
  uint8_t phy_port;  ///< Zero-based physical port specified by application
  size_t numa_node;

  // Others
  Transport_ *transport = nullptr;       ///< The unreliable transport
  HugeAllocator *huge_alloc = nullptr;   ///< This thread's hugepage allocator
  size_t unexp_credits = kRpcPktWindow;  ///< Available unexpected pkt slots

  /// The next request number prefix for each session request window slot
  size_t req_num_arr[Session::kSessionReqWindow] = {0};

  /// The append-only list of session pointers, indexed by session number.
  /// Disconnected sessions are denoted by null pointers. This grows as sessions
  /// are repeatedly connected and disconnected, but 8 bytes per session is OK.
  std::vector<Session *> session_vec;

  /// Sessions for which a management request is in flight. This can be a vector
  /// because session management is not performance-critical.
  std::vector<Session *> mgmt_retry_queue;

  /// Sessions for which (more) request or response packets need to be sent
  std::vector<Session *> datapath_work_queue;

  /// Tx batch information for \p tx_burst
  //@{
  RoutingInfo *tx_routing_info_arr[Transport_::kPostlist];
  MsgBuffer *tx_msg_buffer_arr[Transport_::kPostlist];
  //@}

  /// Rx batch information for \p rx_burst
  Buffer rx_msg_buffer_arr[Transport_::kPostlist];

  SessionMgmtHook sm_hook; /* Shared with Nexus for session management */
  SlowRand slow_rand;

 public:
  // Fault injection for testing

  /// Fail remote routing info resolution at client
  bool testing_fail_resolve_remote_rinfo_client = false;
};

/* Instantiate required Rpc classes so they get compiled for the linker */
template class Rpc<IBTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H
