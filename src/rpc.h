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
#include "util/huge_alloc.h"
#include "util/logger.h"
#include "util/mt_queue.h"
#include "util/rand.h"
#include "util/timer.h"
#include "util/udp_client.h"

namespace erpc {

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
  /// Max request or response *data* size, i.e., excluding packet headers
  static constexpr size_t kMaxMsgSize =
      HugeAlloc::kMaxClassSize -
      ((HugeAlloc::kMaxClassSize / TTr::kMaxDataPerPkt) * sizeof(pkthdr_t));
  static_assert((1ull << kMsgSizeBits) >= kMaxMsgSize, "");
  static_assert((1ull << kPktNumBits) * TTr::kMaxDataPerPkt >= kMaxMsgSize, "");

  /// Initial capacity of the hugepage allocator
  static constexpr size_t kInitialHugeAllocSize = (128 * MB(1));

  /// Duration of a packet loss detection epoch in milliseconds
  static constexpr size_t kPktLossEpochMs = kTesting ? 1 : 10;

  /// Packet loss timeout for an RPC request in milliseconds
  static constexpr size_t kPktLossTimeoutMs = kTesting ? 1 : 500;

  /// Timeout for a session management request in milliseconds
  static constexpr size_t kSMTimeoutMs = kTesting ? 1 : 100;

  /// Reset threshold of the event loop ticker. Assuming each iteration of the
  /// event loop lasts 10 microseconds (it is much smaller in reality), the
  /// time until reset is 10 ms.
  static constexpr size_t kEvLoopTickerReset = 1000;

  //
  // Constructor/destructor (rpc.cc)
  //

  /**
   * @brief Construct the Rpc object from a foreground thread
   * @throw runtime_error if construction fails
   */
  Rpc(Nexus *nexus, void *context, uint8_t rpc_id, sm_handler_t sm_handler,
      uint8_t phy_port = 0, size_t numa_node = 0);

  /// Destroy the Rpc from a foreground thread
  ~Rpc();

  //
  // MsgBuffer management
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
   * \p buf is null) if we ran out of memory.
   *
   * @throw runtime_error if \p size is too large for the allocator, or if
   * hugepage reservation failure is catastrophic. An exception is *not* thrown
   * if allocation fails simply because we ran out of memory.
   */
  inline MsgBuffer alloc_msg_buffer(size_t max_data_size) {
    // This function avoids division for small data sizes
    size_t max_num_pkts = TTr::data_size_to_num_pkts(max_data_size);

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

  /// Resize a MsgBuffer to a smaller size than its max allocation, including
  /// zero size. This does not modify the MsgBuffer's packet headers.
  static inline void resize_msg_buffer(MsgBuffer *msg_buffer,
                                       size_t new_data_size) {
    assert(msg_buffer->is_valid());  // Can be fake
    assert(new_data_size <= msg_buffer->max_data_size);

    // Avoid division for single-packet data sizes
    size_t new_num_pkts = TTr::data_size_to_num_pkts(new_data_size);
    msg_buffer->resize(new_data_size, new_num_pkts);
  }

  /// Free a MsgBuffer created by \p alloc_msg_buffer()
  inline void free_msg_buffer(MsgBuffer msg_buffer) {
    assert(msg_buffer.is_valid_dynamic());

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

  // Methods to bury server-side request and response MsgBuffers. Client-side
  // request and response MsgBuffers are owned by user apps, so eRPC doesn't
  // free their backing memory.

  /**
   * @brief Bury a server sslot's response MsgBuffer (i.e., sslot->tx_msgbuf).
   * This is done in the foreground thread after receiving a packet for the
   * next request.
   *
   * This does not fully validate the MsgBuffer, since we don't want to
   * conditionally bury only initialized MsgBuffers.
   *
   * The server's response MsgBuffers are always backed by dynamic memory, since
   * even prealloc response MsgBuffers are non-fake: the \p prealloc_used field
   * is used to decide if we need to free memory.
   */
  inline void bury_resp_msgbuf_server_st(SSlot *sslot) {
    assert(in_dispatch());
    assert(!sslot->is_client);

    // Free the response MsgBuffer iff it is not preallocated
    if (small_rpc_unlikely(!sslot->prealloc_used)) {
      MsgBuffer *tx_msgbuf = sslot->tx_msgbuf;
      assert(tx_msgbuf->is_valid_dynamic());
      free_msg_buffer(*tx_msgbuf);
      // Need not nullify tx_msgbuf->buffer.buf: we'll just nullify tx_msgbuf
    }

    sslot->tx_msgbuf = nullptr;
  }

  /**
   * @brief Bury a server sslot's request MsgBuffer. This is done in
   * enqueue_response(), so only in the foreground thread.
   *
   * This does not fully validate the MsgBuffer, since we don't want to
   * conditinally bury only initialized MsgBuffers.
   */
  inline void bury_req_msgbuf_server_st(SSlot *sslot) {
    assert(!sslot->is_client);

    MsgBuffer &req_msgbuf = sslot->server_info.req_msgbuf;
    if (small_rpc_unlikely(req_msgbuf.is_dynamic())) {
      // This check is OK, as dynamic MsgBuffers must be initialized
      assert(req_msgbuf.is_valid_dynamic());
      free_msg_buffer(req_msgbuf);
      req_msgbuf.buffer.buf = nullptr;  // Mark invalid for future
    }

    req_msgbuf.buf = nullptr;
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
  int create_session(std::string rem_hostname, uint8_t rem_rpc_id,
                     uint8_t rem_phy_port = 0) {
    return create_session_st(rem_hostname, rem_rpc_id, rem_phy_port);
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

  /// Return true iff this session is connected. The session must not have
  /// been disconnected.
  bool is_connected(int session_num) {
    assert(session_num >= 0 &&
           static_cast<size_t>(session_num) < session_vec.size());

    return session_vec[static_cast<size_t>(session_num)]->is_connected();
  }

 private:
  int create_session_st(std::string rem_hostname, uint8_t rem_rpc_id,
                        uint8_t rem_phy_port = 0);
  int destroy_session_st(int session_num);
  size_t num_active_sessions_st();

  //
  // Session management helper functions
  //
 private:
  // rpc_sm_helpers.cc

  /// Process all session management packets in the hook's RX list and free
  /// them. The handlers for individual request/response types should not free
  /// packets.
  void handle_sm_st();

  /// Free a session's resources and mark it as null in the session vector.
  /// Only the MsgBuffers allocated by the Rpc layer are freed. The user is
  /// responsible for freeing user-allocated MsgBuffers.
  void bury_session_st(Session *);

  /// Send an SM packet. The packet's destination (i.e., client or server) is
  /// determined using the packet's type.
  void sm_pkt_udp_tx_st(const SmPkt &);

  /// Send a session management request for this session and set the SM request
  /// timestamp.
  /// The SM request type is computed using the session state
  void send_sm_req_st(Session *);

  //
  // Session management packet handlers (rpc_connect_handlers.cc,
  // rpc_disconnect_handlers.cc, rpc_reset_handlers.cc)
  //
  void handle_connect_req_st(const SmPkt &);
  void handle_connect_resp_st(const SmPkt &);

  void handle_disconnect_req_st(const SmPkt &);
  void handle_disconnect_resp_st(const SmPkt &);

  /**
   * @brief Try to reset sessions connected to \p rem_hostname.
   * @return True if all such sessions was reset successfully. False if the
   * reset event needs to be queued and processed later.
   */
  bool handle_reset_st(const std::string rem_hostname);

  /// Try to reset a client session. If this is not currently possible, the
  /// session state must be set to reset-in-progress.
  bool handle_reset_client_st(Session *session);

  /// Try to reset a server session. If this is not currently possible, the
  /// session state must be set to reset-in-progress.
  bool handle_reset_server_st(Session *session);

  //
  // Handle available RECVs
  //

  /// Return true iff there are sufficient RECVs available for one session
  bool have_recvs() const {
    return recvs_available >= Session::kSessionCredits;
  }

  /// Allocate RECVs for one session
  void alloc_recvs() {
    assert(have_recvs());
    recvs_available -= Session::kSessionCredits;
  }

  /// Free RECVs allocated for one session
  void free_recvs() {
    recvs_available += Session::kSessionCredits;
    assert(recvs_available <= Transport::kRecvQueueDepth);
  }

  //
  // Rpc datapath (rpc_enqueue_request.cc and rpc_enqueue_response.cc)
  //

 public:
  /**
   * @brief Try to enqueue a request for transmission.
   *
   * If a session slot is available for this session, the request will be
   * enqueued. If this call succeeds, eRPC owns \p msg_buffer until the request
   * completes (i.e., the continuation is invoked).
   *
   * @param session_num The session number to send the request on
   * @param req_type The type of the request
   * @param req_msgbuf The user-created MsgBuffer containing the request payload
   * but not packet headers
   *
   * @param cont_func The continuation function for this request
   * @tag The tag for this request
   * @cont_etid The eRPC thread ID of the background thread to run the
   * continuation on. The default value of \p kInvalidBgETid means that the
   * continuation runs in the foreground. This argument is meant only for
   * internal use by eRPC (i.e., user calls must ignore it).
   *
   * @return 0 on success (i.e., if the request was queued). Negative errno is
   * returned if the request was not queued. -ENOMEM is returned iff the request
   * cannot be enqueued because the session is out of slots.
   */
  int enqueue_request(int session_num, uint8_t req_type, MsgBuffer *req_msgbuf,
                      MsgBuffer *resp_msgbuf, erpc_cont_func_t cont_func,
                      size_t tag, size_t cont_etid = kInvalidBgETid);

  /// The arguments to enqueue_request
  struct enqueue_request_args_t {
    int session_num;
    uint8_t req_type;
    MsgBuffer *req_msgbuf;
    MsgBuffer *resp_msgbuf;
    erpc_cont_func_t cont_func;
    size_t tag;
    size_t cont_etid;

    enqueue_request_args_t() {}
    enqueue_request_args_t(int session_num, uint8_t req_type,
                           MsgBuffer *req_msgbuf, MsgBuffer *resp_msgbuf,
                           erpc_cont_func_t cont_func, size_t tag,
                           size_t cont_etid)
        : session_num(session_num),
          req_type(req_type),
          req_msgbuf(req_msgbuf),
          resp_msgbuf(resp_msgbuf),
          cont_func(cont_func),
          tag(tag),
          cont_etid(cont_etid) {}
  };

  /// Enqueue a response for transmission at the server
  void enqueue_response(ReqHandle *req_handle);

  /// From a continuation, release ownership of a response handle. The response
  /// MsgBuffer is owned by the app and shouldn't be freed.
  inline void release_response(RespHandle *resp_handle) {
    SSlot *sslot = static_cast<SSlot *>(resp_handle);

    // When called from a background thread, enqueue to the foreground thread
    if (small_rpc_unlikely(!in_dispatch())) {
      bg_queues.release_response.unlocked_push(resp_handle);
      return;
    }
    assert(in_dispatch());

    // Request MsgBuffer (tx_msgbuf) was buried when this response was received
    assert(sslot->tx_msgbuf == nullptr);

    Session *session = sslot->session;
    assert(session != nullptr && session->is_client());
    session->client_info.sslot_free_vec.push_back(sslot->index);
  }

  //
  // Event loop
  //

 public:
  /// Run the event loop for \p timeout_ms milliseconds
  inline void run_event_loop(size_t timeout_ms) {
    run_event_loop_timeout_st(timeout_ms);
  }

  /// Run the event loop once
  inline void run_event_loop_once() { run_event_loop_once_st(); }

 private:
  /// Implementation of the run_event_loop(timeout) API function
  void run_event_loop_timeout_st(size_t timeout_ms);

  /// Implementation of run_event_loop() API function
  void run_event_loop_once_st();

  /// Actually run one iteration of the event loop
  void run_event_loop_do_one_st();

  //
  // TX (rpc_tx.cc)
  //

 private:
  /// Return true iff a packet should be dropped
  inline bool roll_pkt_drop() {
    static constexpr uint32_t billion = 1000000000;
    return ((fast_rand.next_u32() % billion) < faults.pkt_drop_thresh_billion);
  }

  /// Enqueue a packet starting at \p offset in \p sslot's \p tx_msgbuf,
  /// possibly deferring transmission. This handles fault injection for dropping
  /// data packets.
  inline void enqueue_pkt_tx_burst_st(const SSlot *sslot, size_t offset,
                                      size_t data_bytes) {
    assert(in_dispatch());
    assert(sslot->tx_msgbuf != nullptr);
    assert(tx_batch_i < TTr::kPostlist);

    const MsgBuffer *tx_msgbuf = sslot->tx_msgbuf;

    Transport::tx_burst_item_t &item = tx_burst_arr[tx_batch_i];
    item.routing_info = sslot->session->remote_routing_info;
    item.msg_buffer = const_cast<MsgBuffer *>(tx_msgbuf);
    item.offset = offset;
    item.data_bytes = data_bytes;

    if (kTesting) {
      item.drop = roll_pkt_drop();
      if (item.drop) {
        LOG_DEBUG(
            "eRPC Rpc %u: Marking packet %s for drop.\n", rpc_id,
            tx_msgbuf->get_pkthdr_str(offset / TTr::kMaxDataPerPkt).c_str());
      }
    } else {
      assert(!item.drop);
    }

    LOG_TRACE("eRPC Rpc %u: Sending packet %s.\n", rpc_id,
              tx_msgbuf->get_pkthdr_str(offset / TTr::kMaxDataPerPkt).c_str());

    tx_batch_i++;
    if (tx_batch_i == TTr::kPostlist) do_tx_burst_st();
  }

  /// Transmit a header-only packet for TX burst, and drain the TX batch.
  /// This handles fault injection for dropping control packets.
  inline void enqueue_hdr_tx_burst_and_drain_st(
      Transport::RoutingInfo *routing_info, MsgBuffer *tx_msgbuf) {
    assert(in_dispatch() && optlevel_large_rpc_supported);
    assert(tx_batch_i < TTr::kPostlist);
    assert(tx_msgbuf->is_expl_cr() || tx_msgbuf->is_req_for_resp());

    Transport::tx_burst_item_t &item = tx_burst_arr[tx_batch_i];
    item.routing_info = routing_info;
    item.msg_buffer = tx_msgbuf;
    item.offset = 0;
    item.data_bytes = 0;

    if (kTesting) {
      item.drop = roll_pkt_drop();
      if (item.drop) {
        LOG_DEBUG("eRPC Rpc %u: Marking packet %s for drop.\n", rpc_id,
                  tx_msgbuf->get_pkthdr_str(0).c_str());
      }
    } else {
      assert(!item.drop);
    }

    LOG_TRACE("eRPC Rpc %u: Sending packet %s.\n", rpc_id,
              tx_msgbuf->get_pkthdr_str().c_str());

    tx_batch_i++;
    do_tx_burst_st();
  }

  /// Transmit packets in the TX batch
  inline void do_tx_burst_st() {
    assert(in_dispatch());
    assert(tx_batch_i > 0);

    dpath_stat_inc(dpath_stats.post_send_calls, 1);
    dpath_stat_inc(dpath_stats.pkts_sent, tx_batch_i);

    transport->tx_burst(tx_burst_arr, tx_batch_i);
    tx_batch_i = 0;
  }

  /// Try to transmit packets for sslots in the datapath TX queue. SSlots for
  /// which all packets can be sent are removed from the queue.
  void process_req_txq_st();

  /**
   * @brief Try to enqueue a single-packet request to the TX batch
   *
   * @param sslot The session slot to send the request for
   * @param req_msgbuf A valid single-packet request MsgBuffer that still needs
   * the packet to be queued
   */
  void tx_small_req_one_st(SSlot *sslot, MsgBuffer *req_msgbuf);

  /**
   * @brief Try to enqueue packets of a multi-packet request to the TX batch
   *
   * @param sslot The session slot to send the request for
   * @param req_msgbuf A valid multi-packet request MsgBuffer that still needs
   * one or more packets to be queued
   */
  void tx_large_req_one_st(SSlot *sslot, MsgBuffer *req_msgbuf);

  //
  // rpc_rx.cc
  //

 public:
  /// Check an RX MsgBuffer submitted to a background thread. It should be
  /// valid, dynamic, and the \p is_req field should match. This holds for
  /// both background request handlers and continuations.
  static void debug_check_bg_rx_msgbuf(SSlot *sslot,
                                       Nexus::BgWorkItemType wi_type);

 private:
  /// Return a credit to this session
  inline void bump_credits(Session *session) {
    assert(session->is_client());
    assert(session->client_info.credits < Session::kSessionCredits);
    session->client_info.credits++;
  }

  /// Copy the data from \p pkt to \p msgbuf
  inline void copy_data_to_msgbuf(MsgBuffer *msgbuf, const uint8_t *pkt) {
    const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);

    size_t offset = pkthdr->pkt_num * TTr::kMaxDataPerPkt;  // rx_msgbuf offset

    size_t bytes_to_copy;
    if (small_rpc_likely(pkthdr->msg_size <= TTr::kMaxDataPerPkt)) {
      bytes_to_copy = pkthdr->msg_size;
    } else {
      size_t num_pkts_in_msg =
          (pkthdr->msg_size + TTr::kMaxDataPerPkt - 1) / TTr::kMaxDataPerPkt;
      bytes_to_copy = (pkthdr->pkt_num == num_pkts_in_msg - 1)
                          ? (pkthdr->msg_size - offset)
                          : TTr::kMaxDataPerPkt;
    }

    assert(bytes_to_copy <= TTr::kMaxDataPerPkt);
    assert(offset + bytes_to_copy <= msgbuf->get_data_size());

    memcpy(reinterpret_cast<char *>(&msgbuf->buf[offset]),
           reinterpret_cast<const char *>(pkt + sizeof(pkthdr_t)),
           bytes_to_copy);
  }

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

  /// Process a credit return
  void process_expl_cr_st(SSlot *sslot, const pkthdr_t *pkthdr);

  /// Process a request-for-response
  void process_req_for_resp_st(SSlot *sslot, const pkthdr_t *pkthdr);

  /// Process a single-packet request message
  void process_small_req_st(SSlot *sslot, const uint8_t *pkt);

  /// Process a single-packet request message
  void process_small_resp_st(SSlot *sslot, const uint8_t *pkt);

  /// Process a packet for a multi-packet request
  void process_large_req_one_st(SSlot *sslot, const uint8_t *pkt);

  /// Process a packet for a multi-packet response
  void process_large_resp_one_st(SSlot *sslot, const uint8_t *pkt);

  /**
   * @brief Submit a work item to a background thread
   *
   * @param sslot Session sslot with a complete request or response MsgBuffer
   * @param wi_type Work item type (request or response)
   * @param bg_etid eRPC thread ID of the background thread to submit to
   */
  void submit_background_st(SSlot *sslot, Nexus::BgWorkItemType wi_type,
                            size_t bg_etid = kMaxBgThreads);

  //
  // Background queue handlers (rpc_bg_queues.cc)
  //
 private:
  /// Process the requests enqueued by background threads
  void process_bg_queues_enqueue_request_st();

  /// Process the responses enqueued by background threads
  void process_bg_queues_enqueue_response_st();

  /// Process the responses freed by background threads
  void process_bg_queues_release_response_st();

  //
  // Fault injection
  //
 public:
  /**
   * @brief Inject a fault that always fails server routing info resolution at
   * all client sessions of this Rpc
   *
   * @throw runtime_error if the caller cannot inject faults
   */
  void fault_inject_fail_resolve_server_rinfo_st();

  /**
   * @brief Set the TX packet drop probability for this Rpc
   * @throw runtime_error if the caller cannot inject faults
   */
  void fault_inject_set_pkt_drop_prob_st(double pkt_drop_prob);

 private:
  /**
   * @brief Check if the caller can inject faults
   * @throw runtime_error if the caller cannot inject faults
   */
  void fault_inject_check_ok() const;

  //
  // Packet loss handling (rpc_pkt_loss.cc)
  //

  /// Scan all outstanding requests for suspected packet loss
  void pkt_loss_scan_reqs_st();

  /// Retransmit packets for an sslot for which we suspect a packet loss
  void pkt_loss_retransmit_st(SSlot *sslot);

  //
  // Stats functions
  //
 public:
  /// Return the average number of packets in a TX batch
  double get_avg_tx_burst_size() const {
    if (!kDatapathStats) return -1;
    return static_cast<double>(dpath_stats.pkts_sent) /
           dpath_stats.post_send_calls;
  }

  /// Reset all datapath stats
  void reset_dpath_stats_st() {
    assert(in_dispatch());
    memset(&dpath_stats, 0, sizeof(dpath_stats));
  }

  //
  // Misc public functions
  //

 public:
  /// Retrieve this Rpc's hugepage allocator. For expert use only.
  inline HugeAlloc *get_huge_alloc() {
    rt_assert(nexus->num_bg_threads == 0,
              "Cannot extract allocator because background threads exist.");
    return huge_alloc;
  }

  /// Return the maximum *data* size that can be sent in one packet
  static inline constexpr size_t get_max_data_per_pkt() {
    return TTr::kMaxDataPerPkt;
  }

  /// Return the maximum message *data* size that can be sent
  static inline size_t get_max_msg_size() { return kMaxMsgSize; }

  /// Return the ID of this Rpc object
  inline uint8_t get_rpc_id() const { return rpc_id; }

  /// Return true iff the caller is running in a background thread
  inline bool in_background() const { return !in_dispatch(); }

  /// Return the eRPC thread ID of the caller
  inline size_t get_etid() const { return tls_registry->get_etid(); }

  /// Busy-sleep for \p ns nanoseconds
  void nano_sleep(size_t ns);

  /// Return RDTSC frequency in GHz
  inline double get_freq_ghz() const { return nexus->freq_ghz; }

  /// Return the number of seconds elapsed since this Rpc was created
  double sec_since_creation();

  /// Return the number of microseconds elapsed since this Rpc was created
  double usec_since_creation();

  //
  // Misc private functions
  //

 private:
  /// Return true iff we're currently running in this Rpc's creator thread
  inline bool in_dispatch() const { return get_etid() == creator_etid; }

  /// Return true iff a user-provided session number is in the session vector
  inline bool is_usr_session_num_in_range_st(int session_num) const {
    assert(in_dispatch());
    return session_num >= 0 &&
           static_cast<size_t>(session_num) < session_vec.size();
  }

  /// Lock the mutex if the Rpc is accessible from multiple threads
  inline void lock_cond(std::mutex *mutex) {
    if (small_rpc_unlikely(multi_threaded)) mutex->lock();
  }

  /// Unlock the mutex if the Rpc is accessible from multiple threads
  inline void unlock_cond(std::mutex *mutex) {
    if (small_rpc_unlikely(multi_threaded)) mutex->unlock();
  }

  // rpc_send_cr.cc

  /**
   * @brief Send a credit return immediately (i.e., no TX burst queueing)
   *
   * @param session The session to send the credit return on
   * @param req_pkthdr The packet header of the request packet that triggered
   * this credit return
   */
  void send_credit_return_now_st(const Session *session,
                                 const pkthdr_t *req_pkthdr);

  // rpc_send_rfr.cc

  /**
   * @brief Send a request-for-response immediately (i.e. no TX burst queueing)
   *
   * @param sslot The session slot to send the request-for-response for
   * @param req_pkthdr The packet header of the response packet that triggered
   * this request-for-response
   */
  void send_req_for_resp_now_st(const SSlot *sslot,
                                const pkthdr_t *resp_pkthdr);

 public:
  // Hooks for apps to modify eRPC behavior

  /// Retry session connection if the remote RPC ID was invalid. This usually
  /// happens when the server RPC thread has not started.
  bool retry_connect_on_invalid_rpc_id = false;

 private:
  // Constructor args
  Nexus *nexus;
  void *context;  ///< The application context
  const uint8_t rpc_id;
  const sm_handler_t sm_handler;
  const uint8_t phy_port;  ///< Zero-based physical port specified by app
  const size_t numa_node;
  const uint64_t creation_tsc;  ///< Timestamp of creation of this Rpc endpoint

  // Derived consts
  const bool multi_threaded;  ///< True iff there are background threads
  const size_t pkt_loss_epoch_cycles;  ///< Packet loss epoch in TSC cycles

  /// A copy of the request/response handlers from the Nexus. We could use
  /// a pointer instead, but an array is faster.
  const std::array<ReqFunc, kReqTypeArraySize> req_func_arr;

  // Rpc metadata
  size_t creator_etid;        ///< eRPC thread ID of the creator thread
  Nexus::Hook nexus_hook;     ///< A hook shared with the Nexus
  TlsRegistry *tls_registry;  ///< Pointer to the Nexus's thread-local registry

  // Sessions

  /// The append-only list of session pointers, indexed by session number.
  /// Disconnected sessions are denoted by null pointers. This grows as sessions
  /// are repeatedly connected and disconnected, but 8 bytes per session is OK.
  std::vector<Session *> session_vec;

  /// The insert-only map of session tokens to index in the session vector
  std::map<sm_uniq_token_t, uint16_t> sm_token_map;

  // Transport
  TTr *transport = nullptr;  ///< The unreliable transport

  /// Current number of RECVs available to use for sessions
  size_t recvs_available = TTr::kRecvQueueDepth;

  Transport::tx_burst_item_t tx_burst_arr[TTr::kPostlist];  ///< Tx batch info
  size_t tx_batch_i = 0;  ///< The batch index for TX burst array

  MsgBuffer rx_msg_buffer_arr[TTr::kPostlist];  ///< Batch info for rx_burst
  uint8_t *rx_ring[TTr::kRecvQueueDepth];       ///< The transport's RX ring
  size_t rx_ring_head = 0;  ///< Current unused RX ring buffer

  // Allocator
  HugeAlloc *huge_alloc = nullptr;  ///< This thread's hugepage allocator
  std::mutex huge_alloc_lock;       ///< A lock to guard the huge allocator

  // RPC work queues

  /// Request sslots that need TX. Response sslots are not queued because they
  /// never stall for credits (or congestion, if CC is implemented).
  std::vector<SSlot *> req_txq;

  /// Queues for datapath API requests from background threads
  struct {
    MtQueue<enqueue_request_args_t> enqueue_request;
    MtQueue<ReqHandle *> enqueue_response;
    MtQueue<RespHandle *> release_response;
  } bg_queues;

  // Packet loss
  size_t prev_epoch_ts;  ///< Timestamp of the previous epoch

  // Misc
  size_t ev_loop_ticker = 0;  ///< Counts event loop iterations until reset
  SlowRand slow_rand;         ///< A slow random generator for "real" randomness
  FastRand fast_rand;         ///< A fast random generator
  UDPClient udp_client;       ///< UDP endpoint used to send SM packets

  /// All the faults that can be injected into eRPC for testing
  struct {
    /// Fail server routing info resolution at client. This is used to test the
    /// case where a client fails to resolve routing info sent by the server.
    bool fail_resolve_server_rinfo = false;

    double pkt_drop_prob = 0.0;  ///< Probability of dropping a packet

    /// A packet is dropped if a random number between zero and one billion
    /// is less than pkt_drop_thresh_billion.
    uint32_t pkt_drop_thresh_billion = 0;
  } faults;

  // Datapath stats
  struct {
    size_t ev_loop_calls = 0;
    size_t pkts_sent = 0;
    size_t post_send_calls = 0;
  } dpath_stats;
};

// Instantiate required Rpc classes so they get compiled for the linker
template class Rpc<IBTransport>;

}  // End erpc

#endif  // ERPC_RPC_H
