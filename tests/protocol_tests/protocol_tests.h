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
static constexpr void *kTestTag = nullptr;
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

    nexus_ = new Nexus("127.0.0.1:31850", kTestNumaNode, 0);
    rt_assert(nexus_ != nullptr, "Failed to create nexus");
    nexus_->register_req_func(kTestReqType, req_handler,
                              ReqFuncType::kForeground);
    nexus_->kill_switch_ = true;  // Kill SM thread

    rpc_ = new Rpc<CTransport>(nexus_, nullptr, kTestRpcId, sm_handler,
                               kTestPhyPort);

    rt_assert(rpc_ != nullptr, "Failed to create Rpc");

    pkthdr_tx_queue_ = &rpc_->testing_.pkthdr_tx_queue_;

    // Init local endpoint
    local_endpoint_.transport_type_ = rpc_->transport_->transport_type_;
    strcpy(local_endpoint_.hostname_, "127.0.0.1");
    local_endpoint_.sm_udp_port_ = 31850;
    local_endpoint_.rpc_id_ = kTestRpcId;
    local_endpoint_.session_num_ = 0;
    rpc_->transport_->fill_local_routing_info(&local_endpoint_.routing_info_);

    // Init remote endpoint. Reusing local routing info & hostname is fine.
    remote_endpoint_.transport_type_ = rpc_->transport_->transport_type_;
    strcpy(remote_endpoint_.hostname_, "127.0.0.1");
    remote_endpoint_.sm_udp_port_ = 31850;
    remote_endpoint_.rpc_id_ = kTestRpcId + 1;
    remote_endpoint_.session_num_ = 1;
    rpc_->transport_->fill_local_routing_info(&remote_endpoint_.routing_info_);

    rpc_->set_context(this);
  }

  ~RpcTest() {
    delete rpc_;
    delete nexus_;
  }

  // Note that the session creation functions below do not use the
  // create_session SM API.

  /// Create a client session in its initial state
  Session *create_client_session_init(const SessionEndpoint client,
                                      const SessionEndpoint server) {
    auto *session = new Session(Session::Role::kClient, kTestUniqToken,
                                rpc_->get_freq_ghz(), kTestLinkBandwidth);
    session->state_ = SessionState::kConnectInProgress;
    session->local_session_num_ = rpc_->session_vec_.size();

    session->client_ = client;
    session->server_ = server;
    session->server_.session_num_ = kInvalidSessionNum;

    rpc_->ring_entries_available_ -= kSessionCredits;
    rpc_->session_vec_.push_back(session);

    return session;
  }

  /// Create a client session in its connected state
  Session *create_client_session_connected(const SessionEndpoint client,
                                           const SessionEndpoint server) {
    create_client_session_init(client, server);
    Session *session = rpc_->session_vec_.back();
    session->server_.session_num_ = server.session_num_;

    auto &remote_rinfo = session->server_.routing_info_;
    rt_assert(rpc_->transport_->resolve_remote_routing_info(&remote_rinfo),
              "Failed to resolve server routing info");

    session->remote_session_num_ = session->server_.session_num_;
    session->state_ = SessionState::kConnected;
    session->client_info_.cc_.prev_desired_tx_tsc_ = rdtsc();

    return session;
  }

  /// Create a server session in its initial state
  Session *create_server_session_init(const SessionEndpoint client,
                                      const SessionEndpoint server) {
    auto *session = new Session(Session::Role::kServer, kTestUniqToken,
                                rpc_->get_freq_ghz(), kTestLinkBandwidth);
    session->state_ = SessionState::kConnected;
    session->client_ = client;
    session->server_ = server;

    for (SSlot &sslot : session->sslot_arr_) {
      sslot.pre_resp_msgbuf_ =
          rpc_->alloc_msg_buffer_or_die(rpc_->transport_->kMaxDataPerPkt);
    }

    auto &remote_rinfo = session->client_.routing_info_;
    rt_assert(rpc_->transport_->resolve_remote_routing_info(&remote_rinfo),
              "Failed to resolve client routing info");

    session->local_session_num_ = session->server_.session_num_;
    session->remote_session_num_ = session->client_.session_num_;

    rpc_->ring_entries_available_ -= kSessionCredits;
    rpc_->session_vec_.push_back(session);
    return session;
  }

  SessionEndpoint get_local_endpoint() const { return local_endpoint_; }
  SessionEndpoint get_remote_endpoint() const { return remote_endpoint_; }

  SessionEndpoint set_invalid_session_num(SessionEndpoint se) {
    se.session_num_ = kInvalidSessionNum;
    return se;
  }

  Rpc<CTransport> *rpc_ = nullptr;
  FixedQueue<pkthdr_t, kSessionCredits> *pkthdr_tx_queue_;

 private:
  Nexus *nexus_ = nullptr;

  /// Endpoint in this Rpc (Rpc ID = kTestRpcId), with session number = 0
  SessionEndpoint local_endpoint_;

  /// A remote endpoint with Rpc ID = kTestRpcId + 1, session number = 1
  SessionEndpoint remote_endpoint_;

  /// Useful counters for subtests
  size_t num_req_handler_calls_ = 0;
  size_t num_cont_func_calls_ = 0;
};

/// The common request handler for subtests. Works for any request size.
/// Copies request to response.
static void req_handler(ReqHandle *req_handle, void *_context) {
  auto *context = static_cast<RpcTest *>(_context);
  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  const size_t resp_size = req_msgbuf->get_data_size();

  req_handle->dyn_resp_msgbuf_ = context->rpc_->alloc_msg_buffer(resp_size);
  memcpy(req_handle->dyn_resp_msgbuf_.buf_, req_msgbuf->buf_, resp_size);

  context->rpc_->enqueue_response(req_handle, &req_handle->dyn_resp_msgbuf_);
  context->num_req_handler_calls_++;
}

/// The common continuation for subtests.
static void cont_func(void *_context, void *) {
  auto *context = static_cast<RpcTest *>(_context);
  context->num_cont_func_calls_++;
}

}  // namespace erpc
