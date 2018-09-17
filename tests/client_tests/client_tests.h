#include <gtest/gtest.h>
#include <string.h>
#include <atomic>
#include <cstring>
#include <thread>

#include "rpc.h"
#include "util/test_printf.h"

using namespace erpc;

static constexpr size_t kTestEventLoopMs = 200;
static constexpr size_t kTestMaxEventLoopMs = 20000;  // 20 seconds
static constexpr uint8_t kTestClientRpcId = 100;
static constexpr uint8_t kTestServerRpcId = 200;
static constexpr uint8_t kTestReqType = 3;

// Running unit tests with DPDK requires two ports
static constexpr uint8_t kTestClientPhyPort = 0;
#ifdef DPDK
static constexpr uint8_t kTestServerPhyPort = kTestClientPhyPort + 1;
#else
static constexpr uint8_t kTestServerPhyPort = kTestClientPhyPort;
#endif
static constexpr size_t kTestNumaNode = 0;

// Shared between client and server thread
std::atomic<size_t> num_servers_up;   ///< Number of ready servers
std::atomic<bool> all_servers_ready;  ///< True iff all server threads are ready
std::atomic<bool> client_done;        ///< True when the client has disconnected

// Hack: The server threads check that their Rpcs have zero active sessions
// after the client exits. This needs to be disabled for test_create_session,
// which does not use disconnection.
bool server_check_all_disconnected = true;

/// Basic context to derive from
class BasicAppContext {
 public:
  bool is_client;
  Rpc<CTransport> *rpc = nullptr;
  int *session_num_arr = nullptr;  ///< Sessions created as client

  size_t num_sm_resps = 0;   ///< Number of SM responses
  size_t num_rpc_resps = 0;  ///< Number of Rpc responses
};

/// Info required to register a request handler function
class ReqFuncRegInfo {
 public:
  const uint8_t req_type;
  const erpc_req_func_t req_func;
  const ReqFuncType req_func_type;

  ReqFuncRegInfo(uint8_t req_type, erpc_req_func_t req_func,
                 ReqFuncType req_func_type)
      : req_type(req_type), req_func(req_func), req_func_type(req_func_type) {}
};

enum class ConnectServers : bool { kTrue, kFalse };

/// Pick a random non-zero message size smaller than \p max_msg_size, with an
/// approximately X% chance of the message fitting in one packet. Other
/// messages have a 80% chance of fitting in 10 packets. This reduces test
/// running time.
size_t get_rand_msg_size(FastRand *fast_rand, size_t max_data_per_pkt,
                         size_t max_msg_size) {
  assert(max_msg_size > max_data_per_pkt * 10);
  // Hack to return some constant data:
  // if (fast_rand != nullptr) return 3000;

  size_t X = 50;
  if (fast_rand->next_u32() % 100 < X) {
    // Choose a single-packet message
    uint32_t sample = fast_rand->next_u32();
    return (sample % max_data_per_pkt) + 1;
  } else {
    if (fast_rand->next_u32() % 100 < 80) {
      // Choose a message size that fits in 1 to 10 packets
      uint32_t num_pkts = (fast_rand->next_u32() % 10) + 1;
      return (fast_rand->next_u32() % (num_pkts * max_data_per_pkt)) + 1;
    } else {
      // Choose any message size up to the max size
      return (fast_rand->next_u32() % max_msg_size) + 1;
    }
  }
}

// Forward declaration
void wait_for_sm_resps_or_timeout(BasicAppContext &, const size_t,
                                  const double);

/// A basic session management handler that expects successful responses
void basic_sm_handler(int session_num, SmEventType sm_event_type,
                      SmErrType sm_err_type, void *_context) {
  _unused(session_num);
  _unused(sm_event_type);
  _unused(sm_err_type);
  _unused(_context);

  auto *context = static_cast<BasicAppContext *>(_context);
  context->num_sm_resps++;

  assert(sm_err_type == SmErrType::kNoError);
  assert(sm_event_type == SmEventType::kConnected ||
         sm_event_type == SmEventType::kDisconnected);
}

/// A basic empty session management handler that should never be invoked.
void basic_empty_sm_handler(int, SmEventType, SmErrType, void *) {
  throw std::runtime_error("Empty SM handler invoked.");
}

/// A basic request handler that should never be invoked
void basic_empty_req_handler(ReqHandle *, void *) {
  throw std::runtime_error("Empty req handler invoked.");
}

/**
 * @brief The basic server thread function
 *
 * @param nexus The process's Nexus
 * @param rpc_id The ID for the Rpc created by this server thread
 * @param sm_handler The SM handler for this server thread
 * @param num_srv_threads The number of server Rpc (foreground) threads
 * @param connect_servers True iff the server threads should be connected
 */
void basic_server_thread_func(Nexus *nexus, uint8_t rpc_id,
                              sm_handler_t sm_handler, size_t num_srv_threads,
                              ConnectServers connect_servers,
                              double pkt_loss_prob) {
  BasicAppContext context;
  context.is_client = false;

  Rpc<CTransport> rpc(nexus, static_cast<void *>(&context), rpc_id, sm_handler,
                      kTestServerPhyPort);
  if (kTesting) rpc.fault_inject_set_pkt_drop_prob_st(pkt_loss_prob);

  context.rpc = &rpc;
  num_servers_up++;

  // Wait for all servers to come up
  while (num_servers_up < num_srv_threads) usleep(1);
  all_servers_ready = true;

  // Connect to all other server threads if needed
  if (connect_servers == ConnectServers::kTrue) {
    assert(num_srv_threads > 1);
    test_printf("test: Server %u connecting to %zu other server threads.\n",
                rpc_id, num_srv_threads - 1);

    // Session number for server (kTestServerRpcId + x) is session_num_arr[x]
    context.session_num_arr = new int[num_srv_threads];

    // Create the sessions
    for (size_t i = 0; i < num_srv_threads; i++) {
      uint8_t other_rpc_id = static_cast<uint8_t>(kTestServerRpcId + i);
      if (other_rpc_id == rpc_id) continue;

      context.session_num_arr[i] = context.rpc->create_session(
          "localhost:31850", kTestServerRpcId + static_cast<uint8_t>(i));
      assert(context.session_num_arr[i] >= 0);
    }

    // Wait for the sessions to connect
    wait_for_sm_resps_or_timeout(context, num_srv_threads - 1, nexus->freq_ghz);
  } else {
    test_printf("Server %u: not connecting to other server threads.\n", rpc_id);
  }

  while (!client_done) {  // Wait for all clients
    rpc.run_event_loop(kTestEventLoopMs);
  }

  // Disconnect sessions created to other server threads if needed
  if (connect_servers == ConnectServers::kTrue) {
    test_printf(
        "Server %u: disconnecting from %zu other server threads. "
        "Current active sessions = %zu.\n",
        rpc_id, num_srv_threads - 1, rpc.num_active_sessions());

    for (size_t i = 0; i < num_srv_threads; i++) {
      uint8_t other_rpc_id = static_cast<uint8_t>(kTestServerRpcId + i);
      if (other_rpc_id == rpc_id) continue;

      context.rpc->destroy_session(context.session_num_arr[i]);
    }

    // We cannot stop running the event loop after receiving the disconnect
    // responses required by this thread. We need to keep the event loop running
    // to send disconnect responses to other server threads.
    context.num_sm_resps = 0;
    while (num_servers_up > 0) {
      rpc.run_event_loop(kTestEventLoopMs);
      if (context.num_sm_resps == num_srv_threads - 1) {
        num_servers_up--;  // Mark this server as down
        context.num_sm_resps = 0;
      }
    }
  }

  if (server_check_all_disconnected) {
    // The client is done only after disconnecting
    assert(rpc.num_active_sessions() == 0);
  }
}

/**
 * @brief Launch (possibly) multiple server threads and one client thread
 *
 * @param num_sessions The number of sessions needed by the client thread,
 * equal to the number of server threads launched
 *
 * @param num_bg_threads The number of background threads in the Nexus. If
 * this is non-zero, the request handler is executed in a background thread.
 *
 * @param client_thread_func The function executed by the client threads.
 * Server threads execute \p basic_server_thread_func()
 *
 * @param req_func_vec The request handlers to register
 * @param connect_servers True if the created server threads should be connected
 * @param srv_pkt_drop_prob The packet drop probability of the server Rpcs
 */
void launch_server_client_threads(
    size_t num_sessions, size_t num_bg_threads,
    void (*client_thread_func)(Nexus *, size_t),
    std::vector<ReqFuncRegInfo> req_func_reg_info_vec,
    ConnectServers connect_servers, double srv_pkt_drop_prob) {
  Nexus nexus("localhost:31850", kTestNumaNode, num_bg_threads);

  // Register the request handler functions
  for (ReqFuncRegInfo &info : req_func_reg_info_vec) {
    nexus.register_req_func(info.req_type, info.req_func, info.req_func_type);
  }

  num_servers_up = 0;
  all_servers_ready = false;
  client_done = false;

  test_printf("test: Using %zu sessions\n", num_sessions);

  std::vector<std::thread> server_threads(num_sessions);

  // Launch one server Rpc thread for each client session
  for (size_t i = 0; i < num_sessions; i++) {
    // Server threads need an SM handler iff we're connecting servers together
    sm_handler_t _sm_handler = connect_servers == ConnectServers::kFalse
                                   ? basic_empty_sm_handler
                                   : basic_sm_handler;

    server_threads[i] = std::thread(
        basic_server_thread_func, &nexus, kTestServerRpcId + i, _sm_handler,
        num_sessions, connect_servers, srv_pkt_drop_prob);
  }

  // Wait for all servers to be ready before launching client thread
  while (!all_servers_ready) usleep(1);

  std::thread client_thread(client_thread_func, &nexus, num_sessions);

  for (auto &thread : server_threads) thread.join();
  client_thread.join();
}

/**
 * @brief Initialize client context and create sessions to server Rpcs running
 * on localhost
 *
 * @param nexus The process's Nexus
 * @param context The uninitialized client context
 * @param num_sessions The number of sessions to create for the client. Session
 * \p i is created to Rpc \p {kTestServerRpcId + i} at localhost
 * @param sm_handler The client's sm handler
 */
void client_connect_sessions(Nexus *nexus, BasicAppContext &context,
                             size_t num_sessions, sm_handler_t sm_handler) {
  assert(num_sessions >= 1);

  // Wait for all server threads to start
  while (!all_servers_ready) usleep(1);

  context.is_client = true;
  context.rpc =
      new Rpc<CTransport>(nexus, static_cast<void *>(&context),
                          kTestClientRpcId, sm_handler, kTestClientPhyPort);

  // Connect the sessions
  context.session_num_arr = new int[num_sessions];
  for (size_t i = 0; i < num_sessions; i++) {
    context.session_num_arr[i] = context.rpc->create_session(
        "localhost:31850", kTestServerRpcId + static_cast<uint8_t>(i));
  }

  while (context.num_sm_resps < num_sessions) {
    context.rpc->run_event_loop(kTestEventLoopMs);
  }

  // basic_sm_handler checks that the callbacks have no errors
  assert(context.num_sm_resps == num_sessions);
}

/**
 * @brief Run the event loop on \p context's Rpc until we get at least
 * \p num_resps session management responses, or until \p kTestMaxEventLoopMs
 * are elapsed
 *
 * @param context The server or client context containing the Rpc
 * @param num_resps The number of SM responses to wait for
 * @param freq_ghz rdtsc frequency in GHz
 */
void wait_for_sm_resps_or_timeout(BasicAppContext &context,
                                  const size_t num_resps,
                                  const double freq_ghz) {
  // Run the event loop for up to kTestMaxEventLoopMs milliseconds
  uint64_t cycles_start = rdtsc();
  while (context.num_sm_resps < num_resps) {
    context.rpc->run_event_loop(kTestEventLoopMs);

    double ms_elapsed = to_msec(rdtsc() - cycles_start, freq_ghz);
    if (ms_elapsed > kTestMaxEventLoopMs) break;
  }
}

/**
 * @brief Run the event loop on \p context's Rpc until we get at least
 * \p num_resps RPC responses, or until \p kTestMaxEventLoopMs are elapsed
 *
 * @param context The server or client context containing the Rpc
 * @param num_resps The number of RPC responses to wait for
 * @param freq_ghz rdtsc frequency in GHz
 */
void wait_for_rpc_resps_or_timeout(BasicAppContext &context,
                                   const size_t num_resps,
                                   const double freq_ghz) {
  // Run the event loop for up to kTestMaxEventLoopMs milliseconds
  uint64_t cycles_start = rdtsc();
  while (context.num_rpc_resps < num_resps) {
    context.rpc->run_event_loop(kTestEventLoopMs);

    double ms_elapsed = to_msec(rdtsc() - cycles_start, freq_ghz);
    if (ms_elapsed > kTestMaxEventLoopMs) break;
  }
}
