#include <gtest/gtest.h>

#define private public
#include "rpc.h"

// These tests never run event loop, so SM pkts sent by Rpc have no consequence
namespace erpc {

static constexpr size_t kTestUdpPort = 3185;
static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kTestNumaNode = 0;
static constexpr size_t kTestUniqToken = 42;
static constexpr size_t kTestRpcId = 0;  // ID of the fixture's Rpc
static constexpr size_t kTestReqType = 1;

typedef IBTransport TestTransport;
extern void req_handler(ReqHandle *, void *);  // Defined in each test.cc

/// Basic eRPC test class with an Rpc object and functions to create client
/// and server sessions
class RpcTest : public ::testing::Test {
 public:
  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcTest() {
    nexus = new Nexus("localhost", kTestUdpPort);
    rt_assert(nexus != nullptr, "Failed to create nexus");
    nexus->drop_all_rx();  // Prevent SM thread from doing any real work
    nexus->register_req_func(kTestReqType,
                             ReqFunc(req_handler, ReqFuncType::kForeground));

    rpc = new Rpc<TestTransport>(nexus, nullptr, kTestRpcId, sm_handler,
                                 kTestPhyPort, kTestNumaNode);
    rt_assert(rpc != nullptr, "Failed to create Rpc");

    // Init local endpoint
    local_endpoint.transport_type = rpc->transport->transport_type;
    strcpy(local_endpoint.hostname, "localhost");
    local_endpoint.phy_port = kTestPhyPort;
    local_endpoint.rpc_id = kTestRpcId;
    local_endpoint.session_num = 0;
    rpc->transport->fill_local_routing_info(&local_endpoint.routing_info);

    // Init remote endpoint. Reusing local routing info & hostname is fine.
    remote_endpoint.transport_type = rpc->transport->transport_type;
    strcpy(remote_endpoint.hostname, "localhost");
    remote_endpoint.phy_port = kTestPhyPort;
    remote_endpoint.rpc_id = kTestRpcId + 1;
    remote_endpoint.session_num = 1;
    rpc->transport->fill_local_routing_info(&remote_endpoint.routing_info);
  }

  ~RpcTest() {
    delete rpc;
    delete nexus;
  }

  /// Create a client session in its initial state
  void create_client_session_init(const SessionEndpoint client,
                                  const SessionEndpoint server) {
    auto *session = new Session(Session::Role::kClient, kTestUniqToken);
    session->state = SessionState::kConnectInProgress;
    session->local_session_num = rpc->session_vec.size();

    session->client = client;
    session->server = server;
    session->server.session_num = kInvalidSessionNum;

    rpc->recvs_available -= Session::kSessionCredits;
    rpc->session_vec.push_back(session);
  }

  /// Create a client session in its initial state
  void create_server_session_init(const SessionEndpoint client,
                                  const SessionEndpoint server) {
    auto *session = new Session(Session::Role::kServer, kTestUniqToken);
    session->state = SessionState::kConnected;
    session->client = client;
    session->server = server;

    for (SSlot &sslot : session->sslot_arr) {
      sslot.pre_resp_msgbuf =
          rpc->alloc_msg_buffer(rpc->transport->kMaxDataPerPkt);
      rt_assert(sslot.pre_resp_msgbuf.buf != nullptr, "Prealloc failed");
    }

    rpc->recvs_available -= Session::kSessionCredits;
    rpc->session_vec.push_back(session);
  }

  SessionEndpoint get_local_endpoint() const { return local_endpoint; }
  SessionEndpoint get_remote_endpoint() const { return remote_endpoint; }

  SessionEndpoint set_invalid_session_num(SessionEndpoint se) {
    se.session_num = kInvalidSessionNum;
    return se;
  }

  Rpc<TestTransport> *rpc = nullptr;

 private:
  Nexus *nexus = nullptr;

  /// Endpoint in this Rpc (Rpc ID = kTestRpcId), with session number = 0
  SessionEndpoint local_endpoint;

  /// A remote endpoint with Rpc ID = kTestRpcId + 1, session number = 1
  SessionEndpoint remote_endpoint;
};

}  // End erpc
