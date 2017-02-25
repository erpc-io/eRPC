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

  /// Initial capacity of the hugepage allocator
  static const size_t kInitialHugeAllocSize = (128 * MB(1));

  /// Max number of unexpected *packets* kept outstanding by this Rpc
  static const size_t kRpcPktWindow = 20;

  // Packet header bitfield sizes
  static const size_t kMsgSizeBits = 24;  ///< Bits for message size
  static const size_t kPktNumBits = 13;   ///< Bits for packet number in request
  static const size_t kReqNumBits = 44;   ///< Bits for request number
  static const size_t kPktHdrMagicBits = 4;  ///< Debug bits for magic number
  static const size_t kPktHdrMagic = 11;  ///< Magic number for packet headers

  static_assert((1ull << kMsgSizeBits) >= kMaxMsgSize, "");
  static_assert(kReqNumBits <= 64, "");
  static_assert(kPktNumBits <= 16, "");
  static_assert((1ull << kPktNumBits) * Transport::kMinMtu >= kMaxMsgSize, "");
  static_assert(kPktHdrMagic < (1ull << kPktHdrMagicBits), "");

  /// The header in each RPC packet
  struct pkthdr_t {
    uint8_t req_type;        /// RPC request type
    uint64_t msg_size : 24;  ///< Total req/resp msg size, excluding headers
    uint64_t rem_session_num : 16;  ///< Session number of the remote session
    uint64_t is_req : 1;            ///< 1 if this packet is a request packet
    uint64_t is_first : 1;     ///< 1 if this packet is the first message packet
    uint64_t is_expected : 1;  ///< 1 if this packet is an "expected" packet
    uint64_t pkt_num : kPktNumBits;     ///< Packet number in the request
    uint64_t req_num : kReqNumBits;     ///< Request number of this packet
    uint64_t magic : kPktHdrMagicBits;  ///< Magic from alloc_pkt_buffer()
  };
  static_assert(sizeof(pkthdr_t) == 16, "");

  /// Error codes returned by the Rpc datapath
  enum class RpcDatapathErrCode : int {
    kInvalidSessionArg,
    kInvalidPktBufferArg,
    kInvalidMsgSizeArg,
    kNoSessionMsgSlots
  };

  static std::string rpc_err_code_str(RpcDatapathErrCode e) {
    switch (e) {
      case RpcDatapathErrCode::kInvalidSessionArg:
        return std::string("[Invalid session argument]");
      case RpcDatapathErrCode::kInvalidPktBufferArg:
        return std::string("[Invalid packet buffer argument]");
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
   * @brief Create a hugepage-backed packet Buffer for the eRPC user. The
   * returned Buffer's \p buf is preceeeded by a packet header that the user
   * must not modify.
   *
   * @param size The minimum number of bytes in the created buffer available
   * to the user
   *
   * @return \p The allocated Buffer. The buffer is invalid if we ran out of
   * memory.
   *
   * @throw runtime_error if \p size is too large for the allocator, or if
   * hugepage reservation failure is catastrophic. An exception is *not* thrown
   * if allocation fails simply because we ran out of memory.
   */
  inline Buffer alloc_pkt_buffer(size_t size) {
    Buffer pkt_buffer = huge_alloc->alloc(size + sizeof(pkthdr_t));

    if (pkt_buffer.is_valid()) {
      pkthdr_t *pkthdr = (pkthdr_t *)pkt_buffer.buf;
      pkthdr->magic = kPktHdrMagic; /* Install the packet header magic */
      pkt_buffer.buf += sizeof(pkthdr_t);
      return pkt_buffer;
    } else {
      return Buffer::get_invalid_buffer();
    }
  }

  /// Free a packet Buffer allocated using alloc_pkt_buffer()
  inline void free_pkt_buffer(Buffer pkt_buffer) {
    assert(pkt_buffer.is_valid());
    pkt_buffer.buf -= sizeof(pkthdr_t); /* Restore pointer for future use */

    pkthdr_t *pkthdr = (pkthdr_t *)pkt_buffer.buf;
    _unused(pkthdr);
    assert(pkthdr->magic == kPktHdrMagic);

    huge_alloc->free_buf(pkt_buffer);
  }

  /// Return a pointer to the packet header of this packet Buffer
  pkthdr_t *pkt_buffer_hdr(Buffer *pkt_buffer) {
    return (pkthdr_t *)(pkt_buffer->buf - sizeof(pkthdr_t));
  }

  /// Check if a packet Buffer's header magic is valid
  inline bool check_pkthdr(Buffer *pkt_buffer) {
    return (pkt_buffer_hdr(pkt_buffer)->magic == kPktHdrMagic);
  }

  // rpc_sm_api.cc

  /**
   * @brief Create a Session and initiate session connection.
   *
   * @return A pointer to the created session if creation succeeds; a callback
   * will be invoked later when connection establishment succeeds/fails.
   * NULL if creation fails; a callback will not be invoked.
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
   *
   * False if the session cannot be disconnected right now since connection
   * establishment is in progress , or if the \p session argument is invalid.
   */
  bool destroy_session(Session *session);

  /// Return the number of active server or client sessions.
  size_t num_active_sessions();

  // rpc_datapath.cc

  /**
   * @brief Try to begin transmission of an RPC request
   *
   * @param session The client session to send the request on
   * @param req_type The type of the request
   * @param buffer The packet buffer containing the request. If this call
   * succeeds, eRPC owns \p buffer until the request completes by invoking
   * the callback.
   *
   * @param msg_size Number of non-header bytes to send from the packet
   * buffer
   *
   * @return 0 on success, i.e., if the request was sent or queued. An error
   * code is returned if the request can neither be sent nor queued.
   */
  int send_request(Session *session, uint8_t req_type, Buffer *buffer,
                   size_t msg_size);

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

  // Constructor args
  Nexus *nexus;
  void *context;  ///< The application context
  uint8_t app_tid;
  session_mgmt_handler_t session_mgmt_handler;
  uint8_t phy_port;
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

  /// Packet batch information for transport
  //@{
  RoutingInfo *routing_info_arr[Transport_::kPostlist];
  Buffer *pkt_buffer_arr[Transport_::kPostlist];
  size_t offset_arr[Transport_::kPostlist];
  //@}

  SessionMgmtHook sm_hook; /* Shared with Nexus for session management */
  SlowRand slow_rand;
};

/* Instantiate required Rpc classes so they get compiled for the linker */
template class Rpc<IBTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H
