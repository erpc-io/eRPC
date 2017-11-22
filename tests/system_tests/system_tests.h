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

typedef IBTransport TestTransport;

/// Basic eRPC test class with an Rpc object and functions to create client
/// and server sessions
class RpcTest : public ::testing::Test {
 public:
  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcTest() {
    nexus = new Nexus("localhost", kTestUdpPort);
    rt_assert(nexus != nullptr, "Failed to create nexus");
    nexus->drop_all_rx();  // Prevent SM thread from doing any real work

    rpc = new Rpc<TestTransport>(nexus, nullptr, kTestRpcId, sm_handler,
                                 kTestPhyPort, kTestNumaNode);
    rt_assert(rpc != nullptr, "Failed to create Rpc");
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

  Nexus *nexus = nullptr;
  Rpc<TestTransport> *rpc = nullptr;
};

}  // End erpc
