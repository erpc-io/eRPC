/**
 * @file protocol_tests.h
 * @brief Tests for the eRPC wire protocol implementation
 *
 * Only real packet orderings are tested. Impossible/buggy packet orderings are
 * ignored: it's OK for eRPC to crash or misbehave with such orderings. For
 * example, the server cannot receive a future request before it sends a
 * response to the current request.
 */

#include <gtest/gtest.h>

#define private public
#include "rpc.h"

// These tests never run event loop, so SM pkts sent by Rpc have no consequence
namespace erpc {

static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kTestNumaNode = 0;
static constexpr size_t kTestUniqToken = 42;
static constexpr size_t kTestRpcId = 0;  // ID of the fixture's Rpc
static constexpr size_t kTestReqType = 1;
static constexpr size_t kTestTag = 0;
static constexpr size_t kTestSmallMsgSize = 32;
static constexpr size_t kTestLargeMsgSize = KB(128);
static constexpr double kTestLinkBandwidth = 56.0 * 1000 * 1000 * 1000 / 8;

static void req_handler(ReqHandle *, void *);  // Defined in each test.cc

/// Basic eRPC test class with an Rpc object and functions to create client
/// and server sessions
class RpcTest : public ::testing::Test {
 public:
  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcTest() {
    if (!kTesting) {
      fprintf(stderr, "Cannot run tests - kTesting is disabled.\n");
      return;
    }

    nexus = new Nexus("localhost:31850", kTestNumaNode, 0);
    rt_assert(nexus != nullptr, "Failed to create nexus");
    nexus->register_req_func(kTestReqType, req_handler,
                             ReqFuncType::kForeground);
    nexus->kill_switch = true;  // Kill SM thread

    rpc = new Rpc<CTransport>(nexus, nullptr, kTestRpcId, sm_handler,
                              kTestPhyPort);

    rt_assert(rpc != nullptr, "Failed to create Rpc");

    pkthdr_tx_queue = &rpc->testing.pkthdr_tx_queue;

    // Init local endpoint
    local_endpoint.transport_type = rpc->transport->transport_type;
    strcpy(local_endpoint.hostname, "localhost");
    local_endpoint.sm_udp_port = 31850;
    local_endpoint.rpc_id = kTestRpcId;
    local_endpoint.session_num = 0;
    rpc->transport->fill_local_routing_info(&local_endpoint.routing_info);

    // Init remote endpoint. Reusing local routing info & hostname is fine.
    remote_endpoint.transport_type = rpc->transport->transport_type;
    strcpy(remote_endpoint.hostname, "localhost");
    remote_endpoint.sm_udp_port = 31850;
    remote_endpoint.rpc_id = kTestRpcId + 1;
    remote_endpoint.session_num = 1;
    rpc->transport->fill_local_routing_info(&remote_endpoint.routing_info);

    rpc->set_context(this);
  }

  ~RpcTest() {
    delete rpc;
    delete nexus;
  }

  // Note that the session creation functions below do not use the
  // create_session SM API.

  /// Create a client session in its initial state
  Session *create_client_session_init(const SessionEndpoint client,
                                      const SessionEndpoint server) {
    auto *session = new Session(Session::Role::kClient, kTestUniqToken,
                                rpc->get_freq_ghz(), kTestLinkBandwidth);
    session->state = SessionState::kConnectInProgress;
    session->local_session_num = rpc->session_vec.size();

    session->client = client;
    session->server = server;
    session->server.session_num = kInvalidSessionNum;

    rpc->ring_entries_available -= kSessionCredits;
    rpc->session_vec.push_back(session);

    return session;
  }

  /// Create a client session in its connected state
  Session *create_client_session_connected(const SessionEndpoint client,
                                           const SessionEndpoint server) {
    create_client_session_init(client, server);
    Session *session = rpc->session_vec.back();
    session->server.session_num = server.session_num;

    auto &remote_rinfo = session->server.routing_info;
    rt_assert(rpc->transport->resolve_remote_routing_info(&remote_rinfo),
              "Failed to resolve server routing info");

    session->remote_session_num = session->server.session_num;
    session->state = SessionState::kConnected;
    return session;
  }

  /// Create a server session in its initial state
  Session *create_server_session_init(const SessionEndpoint client,
                                      const SessionEndpoint server) {
    auto *session = new Session(Session::Role::kServer, kTestUniqToken,
                                rpc->get_freq_ghz(), kTestLinkBandwidth);
    session->state = SessionState::kConnected;
    session->client = client;
    session->server = server;

    for (SSlot &sslot : session->sslot_arr) {
      sslot.pre_resp_msgbuf =
          rpc->alloc_msg_buffer_or_die(rpc->transport->kMaxDataPerPkt);
    }

    auto &remote_rinfo = session->client.routing_info;
    rt_assert(rpc->transport->resolve_remote_routing_info(&remote_rinfo),
              "Failed to resolve client routing info");

    session->local_session_num = session->server.session_num;
    session->remote_session_num = session->client.session_num;

    rpc->ring_entries_available -= kSessionCredits;
    rpc->session_vec.push_back(session);
    return session;
  }

  SessionEndpoint get_local_endpoint() const { return local_endpoint; }
  SessionEndpoint get_remote_endpoint() const { return remote_endpoint; }

  SessionEndpoint set_invalid_session_num(SessionEndpoint se) {
    se.session_num = kInvalidSessionNum;
    return se;
  }

  Rpc<CTransport> *rpc = nullptr;
  FixedQueue<pkthdr_t, kSessionCredits> *pkthdr_tx_queue;

 private:
  Nexus *nexus = nullptr;

  /// Endpoint in this Rpc (Rpc ID = kTestRpcId), with session number = 0
  SessionEndpoint local_endpoint;

  /// A remote endpoint with Rpc ID = kTestRpcId + 1, session number = 1
  SessionEndpoint remote_endpoint;

  /// Useful counters for subtests
  size_t num_req_handler_calls = 0;
  size_t num_cont_func_calls = 0;
};

/// The common request handler for subtests. Works for any request size.
/// Copies request to response.
static void req_handler(ReqHandle *req_handle, void *_context) {
  auto *context = static_cast<RpcTest *>(_context);
  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  const size_t resp_size = req_msgbuf->get_data_size();

  req_handle->dyn_resp_msgbuf = context->rpc->alloc_msg_buffer(resp_size);
  req_handle->prealloc_used = false;
  memcpy(req_handle->dyn_resp_msgbuf.buf, req_msgbuf->buf, resp_size);

  context->rpc->enqueue_response(req_handle);
  context->num_req_handler_calls++;
}

/// The common continuation for subtests.
static void cont_func(RespHandle *, void *_context, size_t) {
  auto *context = static_cast<RpcTest *>(_context);
  context->num_cont_func_calls++;
}

}  // namespace erpc
