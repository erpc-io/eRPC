#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include <random>
#include <vector>
#include "common.h"
#include "msg_buffer.h"
#include "nexus.h"
#include "ops.h"
#include "pkthdr.h"
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
  static const size_t kMaxReqTypes = std::numeric_limits<uint8_t>::max();

  /// Max request or response size, excluding packet headers
  static const size_t kMaxMsgSize = MB(8);
  static_assert((1ull << kMsgSizeBits) >= kMaxMsgSize, "");
  static_assert((1ull << kPktNumBits) * Transport_::kMaxDataPerPkt >=
                    kMaxMsgSize,
                "");

  /// Initial capacity of the hugepage allocator
  static const size_t kInitialHugeAllocSize = (128 * MB(1));

  /// Max number of unexpected *packets* kept outstanding by this Rpc
  static const size_t kRpcUnexpPktWindow = 20;

  /// Error codes returned by the Rpc datapath. 0 means no error.
  enum class RpcDatapathErrCode : int {
    kInvalidSessionArg = 1,
    kInvalidMsgBufferArg,
    kInvalidMsgSizeArg,
    kInvalidReqTypeArg,
    kInvalidOpsArg,
    kNoSessionMsgSlots
  };

  /// Return a string representation of a datapath error code
  static std::string rpc_datapath_err_code_str(int datapath_err_code) {
    auto e = static_cast<RpcDatapathErrCode>(datapath_err_code);

    switch (e) {
      case RpcDatapathErrCode::kInvalidSessionArg:
        return std::string("[Invalid session argument]");
      case RpcDatapathErrCode::kInvalidMsgBufferArg:
        return std::string("[Invalid MsgBuffer argument]");
      case RpcDatapathErrCode::kInvalidMsgSizeArg:
        return std::string("[Invalid message size argument]");
      case RpcDatapathErrCode::kInvalidReqTypeArg:
        return std::string("[Invalid request type argument]");
      case RpcDatapathErrCode::kInvalidOpsArg:
        return std::string("[Invalid Ops argument]");
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
   * @brief Create a hugepage-backed MsgBuffer for the eRPC user.
   * The returned MsgBuffer's \p buf is surrounded by packet headers that the
   * user must not modify. This function does not fill in these message headers,
   * though it sets the magic field in the zeroth header.
   *
   * @param max_data_size Maximum non-header bytes in the returned MsgBuffer
   *
   * @return \p The allocated MsgBuffer. The MsgBuffer is invalid (i.e., its
   * \p buf is NULL) if we ran out of memory.
   *
   * @throw runtime_error if \p size is too large for the allocator, or if
   * hugepage reservation failure is catastrophic. An exception is *not* thrown
   * if allocation fails simply because we ran out of memory.
   */
  inline MsgBuffer alloc_msg_buffer(size_t max_data_size) {
    size_t max_num_pkts;

    /* Avoid division for small packets */
    if (small_msg_likely(max_data_size <= Transport_::kMaxDataPerPkt)) {
      max_num_pkts = 1;
    } else {
      max_num_pkts = (max_data_size + (Transport_::kMaxDataPerPkt - 1)) /
                     Transport_::kMaxDataPerPkt;
    }

    static_assert(is_power_of_two(sizeof(pkthdr_t)), ""); /* For bit shift */
    Buffer buffer =
        huge_alloc->alloc(max_data_size + (max_num_pkts * sizeof(pkthdr_t)));
    if (unlikely(buffer.buf == nullptr)) {
      return MsgBuffer::get_invalid_msgbuf();
    }

    MsgBuffer msg_buffer(buffer, max_data_size, max_num_pkts);

    /* Set the packet header magic in the 0th header */
    pkthdr_t *pkthdr = msg_buffer.get_pkthdr_0();
    pkthdr->magic = kPktHdrMagic;
    return msg_buffer;
  }

  /// Resize a MsgBuffer. This does not modify any headers.
  inline void resize_msg_buffer(MsgBuffer *msg_buffer, size_t new_data_size) {
    assert(msg_buffer != nullptr);
    assert(new_data_size < msg_buffer->max_data_size);
    assert(msg_buffer->check_pkthdr_0());

    size_t new_num_pkts;

    /* Avoid division for small packets */
    if (small_msg_likely(new_data_size <= Transport_::kMaxDataPerPkt)) {
      new_num_pkts = 1;
    } else {
      new_num_pkts = (new_data_size + (Transport_::kMaxDataPerPkt - 1)) /
                     Transport_::kMaxDataPerPkt;
    }

    msg_buffer->resize(new_data_size, new_num_pkts);
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
   * @brief Try to enqueue a request for transmission.
   *
   * If a message slot is available for this session, the request will be
   * enqueued. In this case, a request number is assigned using the slot index,
   * all packet headers of \p are filled in, and \p session is inserted into the
   * datapath TX work queue.
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
  void run_event_loop_one() {
    dpath_stat_inc(&dpath_stats.ev_loop_calls);

    /* Handle session management events, if any */
    if (unlikely(sm_hook.session_mgmt_ev_counter > 0)) {
      handle_session_management(); /* Callee grabs the hook lock */
    }

    /* Check if we need to retransmit any session management requests */
    if (unlikely(mgmt_retry_queue.size() > 0)) {
      mgmt_retry();
    }

    process_completions();            /* RX */
    process_datapath_tx_work_queue(); /* TX */
  }

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

  /// Register application-defined operations for a request type
  int register_ops(uint8_t req_type, Ops app_ops) {
    Ops &arr_ops = ops_arr[req_type];

    /* Check if this request type is already registered */
    if (arr_ops.is_valid()) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidReqTypeArg);
    }

    /* Check if the application's Ops is valid */
    if (!app_ops.is_valid()) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidOpsArg);
    }

    arr_ops = app_ops;
    return 0;
  }

 private:
  // rpc.cc

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

  /// Add \p session to the TX work queue if it's not already present
  inline void upsert_datapath_tx_work_queue(Session *session) {
    assert(session != nullptr);
    if (!session->in_datapath_tx_work_queue) {
      session->in_datapath_tx_work_queue = true;
      datapath_tx_work_queue.push_back(session);
    }
  }

  /// Try to transmit packets for Sessions in the \p datapath_tx_work_queue.
  /// Sessions for which all packets can be sent are removed from the queue.
  void process_datapath_tx_work_queue();

  /**
   * @brief Try to enqueue a single-packet message
   *
   * @param session The session to send the message on. This session is in
   * the datapath TX work queue and it is connected.
   *
   * @param tx_msgbuf A valid single-packet MsgBuffer that needs one packet to
   * be queued
   */
  void process_datapath_tx_work_queue_single_pkt_one(Session *session,
                                                     MsgBuffer *tx_msgbuf);

  /**
   * @brief Try to enqueue a multi-packet message
   *
   * @param session The session to send the message on. This session is in
   * the datapath TX work queue and it is connected.
   *
   * @param tx_msgbuf A valid multi-packet MsgBuffer that needs one or more
   * packets to be queued
   */
  void process_datapath_tx_work_queue_multi_pkt_one(Session *session,
                                                    MsgBuffer *tx_msgbuf);

  /**
   * @brief Process received packets and post RECVs. The ring buffers received
   * from `rx_burst` must not be used after new RECVs are posted.
   *
   * Although none of the polled RX ring buffers can be overwritten by the
   * NIC until we send at least one response/CR packet back, we do not control
   * the order or time at which these packets are sent, due to constraints like
   * session credits and packet pacing.
   */
  void process_completions();

  /**
   * @brief Process a request or response packet received for a small message.
   * This packet cannot be a credit return.
   *
   * @param session The session that the message was received on. The session
   * is connected.
   *
   * @param pkt The received packet. The zeroth byte of this packet is the
   * eRPC packet header.
   */
  void process_completions_small_msg_one(Session *session, uint8_t *pkt);

  /**
   * @brief Process a request or response packet received for a large message.
   * This packet cannot be a credit return.
   *
   * @param session The session that the message was received on. The session
   * is connected.
   *
   * @param pkt The received packet. The zeroth byte of this packet is the
   * eRPC packet header.
   */
  void process_completions_large_msg_one(Session *session, uint8_t *pkt);

  // Constructor args
  Nexus *nexus;
  void *context;  ///< The application context
  uint8_t app_tid;
  session_mgmt_handler_t session_mgmt_handler;
  uint8_t phy_port;  ///< Zero-based physical port specified by application
  size_t numa_node;

  // Others
  Transport_ *transport = nullptr;      ///< The unreliable transport
  HugeAllocator *huge_alloc = nullptr;  ///< This thread's hugepage allocator
  size_t unexp_credits = kRpcUnexpPktWindow;  ///< Available unexpe pkt slots

  uint8_t *rx_ring[Transport_::kRecvQueueDepth];  ///< The transport's RX ring
  size_t rx_ring_head = 0;  ///< Current unused RX ring buffer

  Ops ops_arr[kMaxReqTypes];

  /// The next request number prefix for each session request window slot
  size_t req_num_arr[Session::kSessionReqWindow] = {0};

  /// The append-only list of session pointers, indexed by session number.
  /// Disconnected sessions are denoted by null pointers. This grows as sessions
  /// are repeatedly connected and disconnected, but 8 bytes per session is OK.
  std::vector<Session *> session_vec;

  /// Sessions for which a management request is in flight. This can be a vector
  /// because session management is not performance-critical.
  std::vector<Session *> mgmt_retry_queue;

  /// Sessions that need TX. We don't need a std::list to support efficient
  /// deletes because sessions are implicitly deleted while processing the
  /// work queue.
  std::vector<Session *> datapath_tx_work_queue;

  /// Tx batch information for interfacing between the event loop and the
  /// transport.
  tx_burst_item_t tx_burst_arr[Transport_::kPostlist];
  size_t tx_batch_i;  ///< The batch index for \p tx_burst_arr

  /// Rx batch information for \p rx_burst
  MsgBuffer rx_msg_buffer_arr[Transport_::kPostlist];

  SessionMgmtHook sm_hook; /* Shared with Nexus for session management */
  SlowRand slow_rand;

  // Stats
  struct {
    size_t ev_loop_calls = 0;
    size_t unexp_credits_exhausted = 0;
  } dpath_stats;

 public:
  // Fault injection for testing. These cannot be static or const.

  /// Fail remote routing info resolution at client. This is used to test the
  /// case where the server creates a session in response to a connect request,
  /// but the client fails to resolve the routing info sent by the server.
  bool testing_fail_resolve_remote_rinfo_client = false;
};

/* Instantiate required Rpc classes so they get compiled for the linker */
template class Rpc<IBTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H
