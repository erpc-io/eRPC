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

#define rpc_dprintf(fmt, ...)            \
  do {                                   \
    if (RPC_DPRINTF) {                   \
      fprintf(stderr, fmt, __VA_ARGS__); \
      fflush(stderr);                    \
    }                                    \
  } while (0)

// Per-thread RPC object
template <class Transport_>
class Rpc {
 public:
  static const size_t kMaxMsgSize = MB(8);  ///< Max request/response size
  static const size_t kReqNumBits = 48;     ///< Bits for request number
  static const size_t kPktNumBits = 14;  ///< Bits for packet number in request
  static const size_t kRpcWindowSize = 20;  ///< Max outstanding pkts per Rpc
  static const size_t kInitialHugeAllocSize =
      (128 * MB(1));  ///< Initial capacity of the hugepage allocator

  static_assert(kReqNumBits <= 64, "");
  static_assert(kPktNumBits <= 16, "");
  static_assert((1ull << kPktNumBits) * Transport::kMinMtu >= kMaxMsgSize, "");

  /// The header in each RPC packet
  struct pkthdr_t {
    uint32_t tot_size;         ///< Total message size across multiple packets
    uint16_t rem_session_num;  ///< Session number of the remote packet target
    uint32_t is_req : 1;       ///< 1 if this packet is a request packet
    uint32_t is_first : 1;     ///< 1 if this packet is the first message packet
    uint32_t pkt_num : kPktNumBits;  ///< Packet number in the request
    uint64_t req_num : kReqNumBits;  ///< Request number of this packet
  };
  static_assert(sizeof(pkthdr_t) == 16, "");

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
   * @brief Create a Buffer for the eRPC user application
   * @param size The minimum size of the created Buffer. The size of the
   * allocated buffer can be larger than \p size.
   *
   * @return \p The allocated Buffer. The buffer is invalid if we ran out of
   * memory.
   *
   * @throw runtime_error if \p size is invalid, or if hugepage reservation
   * failure is catastrophic (i.e., an exception is *not* thrown if allocation
   * fails simply because we ran out of memory).
   */
  inline Buffer alloc(size_t size) { return huge_alloc->alloc(size); }

  /// Free the buffer
  inline void free_buf(Buffer buffer) { huge_alloc->free_buf(buffer); }

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
  void send_request(const Session *session, const Buffer *buffer);
  void send_response(const Session *session, const Buffer *buffer);

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

  /// Check if this session pointer is a client session in this Rpc's sessions
  bool is_session_ptr_client(Session *session);

  /// Check if this session pointer is a server session in this Rpc's sessions
  bool is_session_ptr_server(Session *session);

  /// Process all session management events in the queue and free them.
  /// The handlers for individual request/response types should not free
  /// packets.
  void handle_session_management();

  /// Destroy a session object and mark it as NULL in the session vector
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
  void *context; /* The application context */
  uint8_t app_tid;
  session_mgmt_handler_t session_mgmt_handler;
  uint8_t phy_port;
  size_t numa_node;

  // Others
  Transport_ *transport; /* The unreliable transport */

  /*
   * The hugepage allocator used for memory allocation by this Rpc and its
   * member objects. Using one allocator for all hugepage allocations in this
   * thread allows easier memory use accounting.
   */
  HugeAllocator *huge_alloc;

  /*
   * The append-only list of session pointers, indexed by session num.
   * Disconnected sessions are denoted by null pointers. This grows as sessions
   * are repeatedly connected and disconnected, but 8 bytes per session is OK.
   */
  std::vector<Session *> session_vec;

  /* List of sessions for which a management request is in flight */
  std::vector<Session *> mgmt_retry_queue;

  SessionMgmtHook sm_hook; /* Shared with Nexus for session management */
  SlowRand slow_rand;
};

/* Instantiate required Rpc classes so they get compiled for the linker */
template class Rpc<IBTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H
