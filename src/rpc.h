#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include <algorithm>
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
#include "util/gettid.h"
#include "util/huge_alloc.h"
#include "util/mt_list.h"
#include "util/rand.h"
#include "util/tsc.h"

namespace ERpc {

/**
 * @brief Rpc object created by foreground threads, and possibly shared with
 * background threads.
 *
 * Non-const functions that are not thread-safe should be marked in the
 * documentation.
 *
 * @tparam TTr The unreliable transport
 */
template <class TTr>
class Rpc {
 public:
  /// Max request or response size, excluding packet headers
  static constexpr size_t kMaxMsgSize =
      HugeAlloc::kMaxClassSize -
      ((HugeAlloc::kMaxClassSize / TTr::kMaxDataPerPkt) * sizeof(pkthdr_t));
  static_assert((1ull << kMsgSizeBits) >= kMaxMsgSize, "");
  static_assert((1ull << kPktNumBits) * TTr::kMaxDataPerPkt >= kMaxMsgSize, "");

  /// Initial capacity of the hugepage allocator
  static constexpr size_t kInitialHugeAllocSize = (128 * MB(1));

  /// Max number of unexpected *packets* kept outstanding by this Rpc
  static constexpr size_t kRpcUnexpPktWindow = 20;

  /// Return the maximum data size that can be sent in one packet
  static inline constexpr size_t max_data_per_pkt() {
    return TTr::kMaxDataPerPkt;
  }

  //
  // Constructor/destructor (rpc.cc)
  //

  /**
   * @brief Construct the Rpc object from a foreground thread
   * @throw runtime_error if construction fails
   */
  Rpc(Nexus *nexus, void *context, uint8_t app_tid,
      session_mgmt_handler_t session_mgmt_handler, uint8_t phy_port = 0,
      size_t numa_node = 0);

  /// Destroy the Rpc from a foreground thread
  ~Rpc();

  //
  // MsgBuffer allocation
  //

  /**
   * @brief Create a hugepage-backed MsgBuffer for the eRPC user.
   *
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
    if (small_rpc_likely(max_data_size <= TTr::kMaxDataPerPkt)) {
      max_num_pkts = 1;
    } else {
      max_num_pkts =
          (max_data_size + (TTr::kMaxDataPerPkt - 1)) / TTr::kMaxDataPerPkt;
    }

    lock_cond(&huge_alloc_lock);
    Buffer buffer =
        huge_alloc->alloc(max_data_size + (max_num_pkts * sizeof(pkthdr_t)));
    unlock_cond(&huge_alloc_lock);

    if (unlikely(buffer.buf == nullptr)) {
      MsgBuffer msg_buffer;
      msg_buffer.buf = nullptr;
      return msg_buffer;
    }

    MsgBuffer msg_buffer(buffer, max_data_size, max_num_pkts);
    return msg_buffer;
  }

  /// Resize a MsgBuffer to a smaller size than its max allocation.
  /// This does not modify the MsgBuffer's packet headers.
  static inline void resize_msg_buffer(MsgBuffer *msg_buffer,
                                       size_t new_data_size) {
    assert(msg_buffer != nullptr);
    assert(msg_buffer->buf != nullptr && msg_buffer->check_magic());
    assert(new_data_size < msg_buffer->max_data_size);

    size_t new_num_pkts;

    /* Avoid division for small packets */
    if (small_rpc_likely(new_data_size <= TTr::kMaxDataPerPkt)) {
      new_num_pkts = 1;
    } else {
      new_num_pkts =
          (new_data_size + (TTr::kMaxDataPerPkt - 1)) / TTr::kMaxDataPerPkt;
    }

    msg_buffer->resize(new_data_size, new_num_pkts);
  }

  /// Free a MsgBuffer created by \p alloc_msg_buffer()
  inline void free_msg_buffer(MsgBuffer msg_buffer) {
    assert(msg_buffer.is_dynamic() && msg_buffer.check_magic());

    lock_cond(&huge_alloc_lock);
    huge_alloc->free_buf(msg_buffer.buffer);
    unlock_cond(&huge_alloc_lock);
  }

  /// Return the total amount of memory allocated to the user
  inline size_t get_stat_user_alloc_tot() {
    lock_cond(&huge_alloc_lock);
    size_t ret = huge_alloc->get_stat_user_alloc_tot();
    unlock_cond(&huge_alloc_lock);
    return ret;
  }

  //
  // Session management API (rpc_sm_api.cc)
  //

 public:
  /**
   * @brief Create a session and initiate session connection. This function
   * can only be called from the creator thread.
   *
   * @return The local session number (>= 0) of the session if creation succeeds
   * and the connect request is sent, negative errno otherwise.
   *
   * A callback of type \p kConnected or \p kConnectFailed will be invoked if
   * this call is successful.
   */
  int create_session(const char *_rem_hostname, uint8_t rem_app_tid,
                     uint8_t rem_phy_port = 0) {
    return create_session_st(_rem_hostname, rem_app_tid, rem_phy_port);
  }

  /**
   * @brief Disconnect and destroy a client session. The session should not
   * be used by the application after this function is called. This functio
   * can only be called from the creator thread.
   *
   * @param session_num A session number returned from a successful
   * create_session()
   *
   * @return 0 if (a) the session disconnect packet was sent, and the disconnect
   * callback will be invoked later. Negative errno if the session cannot be
   * disconnected.
   */
  int destroy_session(int session_num) {
    return destroy_session_st(session_num);
  }

  /// Return the number of active server or client sessions. This function
  /// can be called only from the creator thread.
  size_t num_active_sessions() { return num_active_sessions_st(); }

 private:
  int create_session_st(const char *_rem_hostname, uint8_t rem_app_tid,
                        uint8_t rem_phy_port = 0);
  int destroy_session_st(int session_num);
  size_t num_active_sessions_st();

  //
  // Rpc datapath (rpc_enqueue_request.cc and rpc_enqueue_response.cc)
  //

 public:
  /**
   * @brief Try to enqueue a request for transmission.
   *
   * If a message slot is available for this session, the request will be
   * enqueued. If this call succeeds, eRPC owns \p msg_buffer until the request
   * completes (i.e., the continuation is invoked).
   *
   * @param session_num The session number to send the request on
   * @param req_type The type of the request
   * @param msg_buffer The user-created MsgBuffer containing the request payload
   * but not packet headers
   * @param cont_func The continuation function for this request
   * @tag The tag for this request
   *
   * @return 0 on success (i.e., if the request was queued). Negative errno is
   * returned if the request was not queued.
   */
  int enqueue_request(int session_num, uint8_t req_type, MsgBuffer *msg_buffer,
                      erpc_cont_func_t cont_func, size_t tag);

  /// Enqueue a response for transmission at the server
  void enqueue_response(ReqHandle *req_handle);

  /// Release a response received at a client
  inline void release_respone(RespHandle *resp_handle) {
    assert(resp_handle != nullptr);

    SSlot *sslot = (SSlot *)resp_handle;
    assert(sslot->tx_msgbuf == nullptr); /* Freed before calling continuation */

    /*
     * Bury the possibly-dynamic response MsgBuffer (rx_msgbuf). If we used
     * pre_resp_msgbuf in the continuation, this is still OK and sufficient.
     */
    bury_sslot_rx_msgbuf(sslot);

    Session *session = sslot->session;
    assert(session->is_client());

    lock_cond(&session->lock);
    session->sslot_free_vec.push_back(sslot->index);
    unlock_cond(&session->lock);
  }

  /// Return true iff the caller is running in a background thread
  inline bool in_background() { return !in_creator(); }

  //
  // Event loop
  //
 public:
  /// Run one iteration of the event loop
  inline void run_event_loop_one() { run_event_loop_one_st(); }

  /// Run the event loop forever
  inline void run_event_loop() { run_event_loop_st(); }

  /// Run the event loop for \p timeout_ms milliseconds
  inline void run_event_loop_timeout(size_t timeout_ms) {
    run_event_loop_timeout_st(timeout_ms);
  }

 private:
  void run_event_loop_one_st();
  void run_event_loop_st();
  void run_event_loop_timeout_st(size_t timeout_ms);

  //
  // Misc private functions
  //

 private:
  /// Return true iff we're currently running in this Rpc's creator.
  /// This is pretty fast as we cache the OS thread ID in \p gettid().
  inline bool in_creator() { return gettid() == creator_os_tid; }

  /// Return true iff a user-provide session number is in the session vector
  inline bool is_usr_session_num_in_range(int session_num) {
    return session_num >= 0 && (size_t)session_num < session_vec.size();
  }

  /// Lock the mutex if the Rpc is accessible from multiple threads
  inline void lock_cond(std::mutex *mutex) {
    if (small_rpc_unlikely(multi_threaded)) {
      mutex->lock();
    }
  }

  /// Unlock the mutex if the Rpc is accessible from multiple threads
  inline void unlock_cond(std::mutex *mutex) {
    if (small_rpc_unlikely(multi_threaded)) {
      mutex->unlock();
    }
  }

  // rpc.cc

  /// Process all session management events in the queue and free them.
  /// The handlers for individual request/response types should not free
  /// packets.
  void handle_session_management_st();

  /// Free a session's resources and mark it as NULL in the session vector.
  /// Only the MsgBuffers allocated by the Rpc layer are freed. The user is
  /// responsible for freeing user-allocated MsgBuffers.
  void bury_session_st(Session *session);

  // rpc_connect_handlers.cc
  void handle_connect_req_st(SessionMgmtPkt *pkt);
  void handle_connect_resp_st(SessionMgmtPkt *pkt);

  // rpc_disconnect_handlers.cc
  void handle_disconnect_req_st(SessionMgmtPkt *pkt);
  void handle_disconnect_resp_st(SessionMgmtPkt *pkt);

  // rpc_sm_retry.cc

  /// Send a (possibly retried) connect request for this session
  void send_connect_req_one_st(Session *session);

  /// Send a (possibly retried) disconnect request for this session
  void send_disconnect_req_one_st(Session *session);

  ///@{
  /// Management retry queue functions. These can only be executed by the Rpc
  /// creator thread.
  void mgmt_retryq_add_st(Session *session);
  void mgmt_retryq_remove_st(Session *session);
  bool mgmt_retryq_contains_st(Session *session);
  void mgmt_retry();
  ///@}

  /// Add \p sslot to the datapath TX queue
  inline void dpath_txq_push_back(SSlot *sslot) {
    lock_cond(&dpath_txq_lock);
    assert(sslot != nullptr);
    assert(std::find(dpath_txq.begin(), dpath_txq.end(), sslot) ==
           dpath_txq.end());
    dpath_txq.push_back(sslot);
    unlock_cond(&dpath_txq_lock);
  }

  // rpc_tx.cc

  /// Try to transmit packets for sslots in the datapath TX queue. SSlots for
  /// which all packets can be sent are removed from the queue.
  void process_dpath_txq_st();

  /**
   * @brief Try to enqueue a single-packet message
   *
   * @param session The session to send the message on. This session must be
   * connected.
   *
   * @param tx_msgbuf A valid single-packet MsgBuffer that needs one packet to
   * be queued
   */
  void process_dpath_txq_small_msg_one_st(Session *session,
                                          MsgBuffer *tx_msgbuf);

  /**
   * @brief Try to enqueue a multi-packet message
   *
   * @param session The session to send the message on. This seession must be
   * connected.
   *
   * @param tx_msgbuf A valid multi-packet MsgBuffer that needs one or more
   * packets to be queued
   */
  void process_dpath_txq_large_msg_one_st(Session *session,
                                          MsgBuffer *tx_msgbuf);

  // rpc_rx.cc

  /**
   * @brief Process received packets and post RECVs. The ring buffers received
   * from `rx_burst` must not be used after new RECVs are posted.
   *
   * Although none of the polled RX ring buffers can be overwritten by the
   * NIC until we send at least one response/CR packet back, we do not control
   * the order or time at which these packets are sent, due to constraints like
   * session credits and packet pacing.
   */
  void process_comps_st();

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
  void process_comps_small_msg_one_st(Session *session, const uint8_t *pkt);

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
  void process_comps_large_msg_one_st(Session *session, const uint8_t *pkt);

  /// Submit a work item for background processing
  void submit_background_st(SSlot *sslot);

  // rpc_send_cr.cc

  /**
   * @brief Send a credit return for this session immediately, i.e., the
   * transmission of the packet must not be rescheduled
   *
   * @param session The session to send the credit return for
   * @param unexp_pkthdr The packet header of the Unexpected packet that
   * triggered this credit return
   */
  void send_credit_return_now_st(Session *session,
                                 const pkthdr_t *unexp_pkthdr);

  /**
   * @brief Free the session slot's TX MsgBuffer if it is dynamic, and NULL-ify
   * it in any case. This does not fully validate the MsgBuffer, since we don't
   * want to conditionally bury only initialized sslots.
   *
   * This is thread-safe, as \p free_msg_buffer() is thread-safe.
   */
  inline void bury_sslot_tx_msgbuf(SSlot *sslot) {
    assert(sslot != nullptr);

    /*
     * The TX MsgBuffer used dynamic allocation if its buffer.buf is non-NULL.
     * Its buf can be non-NULL even when dynamic allocation is not used.
     */
    MsgBuffer *tx_msgbuf = sslot->tx_msgbuf;
    if (small_rpc_unlikely(tx_msgbuf != nullptr && tx_msgbuf->is_dynamic())) {
      /* This check is OK, as dynamic sslots must be initialized */
      assert(tx_msgbuf->buf != nullptr && tx_msgbuf->check_magic());
      free_msg_buffer(*tx_msgbuf);
      /* Need not nullify tx_msgbuf->buffer.buf: we'll just nullify tx_msgbuf */
    }

    sslot->tx_msgbuf = nullptr;
  }

  /**
   * @brief Bury a session slot's TX MsgBuffer without freeing possibly
   * dynamically allocated memory.
   *
   * This is used for burying the TX MsgBuffer used for requests, since the
   * application will free the associated dynamic memory.
   */
  static inline void bury_sslot_tx_msgbuf_nofree(SSlot *sslot) {
    assert(sslot != nullptr);
    sslot->tx_msgbuf = nullptr;
  }

  /**
   * @brief Free the session slot's RX MsgBuffer if it is dynamic, and NULL-ify
   * it in any case. This does not fully validate the MsgBuffer, since we don't
   * want to conditinally bury only initialized sslots.
   *
   * This is thread-safe, as \p free_msg_buffer() is thread-safe.
   */
  inline void bury_sslot_rx_msgbuf(SSlot *sslot) {
    assert(sslot != nullptr);

    /*
     * The RX MsgBuffer used dynamic allocation if its buffer.buf is non-NULL.
     * Its buf can be non-NULL even when dynamic allocation is not used.
     */
    MsgBuffer &rx_msgbuf = sslot->rx_msgbuf;
    if (small_rpc_unlikely(rx_msgbuf.is_dynamic())) {
      /* This check is OK, as dynamic sslots must be initialized */
      assert(rx_msgbuf.buf != nullptr && rx_msgbuf.check_magic());
      free_msg_buffer(rx_msgbuf);
      rx_msgbuf.buffer.buf = nullptr; /* Mark invalid for future */
    }

    rx_msgbuf.buf = nullptr;
  }

  // Constructor args
  Nexus *nexus;
  void *context;  ///< The application context
  uint8_t app_tid;
  session_mgmt_handler_t session_mgmt_handler;
  uint8_t phy_port;  ///< Zero-based physical port specified by application
  size_t numa_node;

  // Others
  const int creator_os_tid;   ///< OS thread ID of the creator thread
  const bool multi_threaded;  ///< True iff there are background threads
  bool in_event_loop;  ///< Track event loop reentrance (w/ kDatapathChecks)

  TTr *transport = nullptr;  ///< The unreliable transport

  HugeAlloc *huge_alloc = nullptr;  ///< This thread's hugepage allocator
  std::mutex huge_alloc_lock;       ///< A lock to guard the huge allocator

  uint8_t *rx_ring[TTr::kRecvQueueDepth];  ///< The transport's RX ring
  size_t rx_ring_head = 0;                 ///< Current unused RX ring buffer

  size_t unexp_credits = kRpcUnexpPktWindow;  ///< Available Unexp pkt slots

  /// A copy of the request/response handlers from the Nexus. We could use
  /// a pointer instead, but an array is faster.
  const std::array<ReqFunc, kMaxReqTypes> req_func_arr;

  /// The next request number prefix for each session request window slot
  size_t req_num_arr[Session::kSessionReqWindow] = {0};
  std::mutex req_num_arr_lock;

  /// The append-only list of session pointers, indexed by session number.
  /// Disconnected sessions are denoted by null pointers. This grows as sessions
  /// are repeatedly connected and disconnected, but 8 bytes per session is OK.
  std::vector<Session *> session_vec;

  /// Sessions for which a management request is in flight. This is only
  /// accessed by the creator thread, so we don't need a lock.
  std::vector<Session *> mgmt_retry_queue;

  /// SSlots that need TX. We don't need a std::list to support efficient
  /// deletes because sslots are implicitly deleted while processing the
  /// work queue.
  std::vector<SSlot *> dpath_txq;
  std::mutex dpath_txq_lock;

  /// Tx batch information for interfacing between the event loop and the
  /// transport.
  tx_burst_item_t tx_burst_arr[TTr::kPostlist];
  size_t tx_batch_i;  ///< The batch index for \p tx_burst_arr

  /// Rx batch information for \p rx_burst
  MsgBuffer rx_msg_buffer_arr[TTr::kPostlist];

  NexusHook nexus_hook; /* A hook shared with the Nexus */
  SlowRand slow_rand;
  FastRand fast_rand;

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
