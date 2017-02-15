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

static const size_t kSessionCredits = 8;  ///< Credits per session endpoint
static const size_t kRpcWindowSize = 20;  ///< Max outstanding pkts per Rpc
static const size_t kInitialHugeAllocSize = (128 * MB(1));
static const size_t kSessionMgmtRetransMs = 5;  ///< Timeout for management reqs
static const size_t kSessionMgmtTimeoutMs = 50;  ///< Max time for mgmt reqs

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
  const uint64_t kStartSeqMask = ((1ull << 48) - 1ull);

  struct in_flight_req_t {
    uint64_t prev_tsc; /* The tsc at which a request was last sent */
    Session *session;

    in_flight_req_t(uint64_t tsc, Session *session)
        : prev_tsc(tsc), session(session) {}

    /*
     * The in-flight request vector contains distinct sessions, so we can
     * ignore the timestamp for comparison.
     */
    bool operator==(const in_flight_req_t &other) {
      return (session == other.session);
    }
  };

 public:
  // rpc.cc

  /**
   * @brief Construct the Rpc object
   * @throw \p runtime_error if construction fails
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
   * @return The allocated Buffer. The buffer is invalid if we ran out of
   * memory.
   *
   * @throw \p runtime_error if \p size is invalid, or if hugepage reservation
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

  /// Generate the start sequence number for a session when it is created
  uint64_t gen_start_seq() { return (slow_rand.next_u64() & kStartSeqMask); }

  // rpc_connect_handlers.cc
  void handle_session_connect_req(SessionMgmtPkt *pkt);
  void handle_session_connect_resp(SessionMgmtPkt *pkt);

  // rpc_disconnect_handlers.cc
  void handle_session_disconnect_req(SessionMgmtPkt *pkt);
  void handle_session_disconnect_resp(SessionMgmtPkt *pkt);

  // rpc_sm_retry.cc
  void send_connect_req_one(Session *session);
  void send_disconnect_req_one(Session *session);
  void add_to_in_flight(Session *session);
  bool is_in_flight(Session *session);
  void remove_from_in_flight(Session *session);

  /// Retry in-flight requests whose retry timeout has expired.
  void retry_in_flight();

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
   * member objects. Using one allocator for all (large) allocations in this
   * thread allows easier memory use accounting.
   */
  HugeAllocator *huge_alloc;

  /*
   * The append-only list of session pointers, indexed by session num.
   * Disconnected sessions are denoted by null pointers. This grows as sessions
   * are repeatedly connected and disconnected, but 8 bytes per session is OK.
   */
  std::vector<Session *> session_vec;

  /*
   * List of client sessions for which session management requests are in
   * flight.
   */
  std::vector<in_flight_req_t> in_flight_vec;

  SessionMgmtHook sm_hook; /* Shared with Nexus for session management */
  SlowRand slow_rand;
};

/* Instantiate required Rpc classes so they get compiled for the linker */
template class Rpc<IBTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H
