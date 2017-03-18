#include <gtest/gtest.h>
#include <string.h>
#include <atomic>
#include <thread>
#include "rpc.h"
#include "test_printf.h"

using namespace ERpc;

static constexpr uint16_t kAppNexusUdpPort = 31851;
static constexpr double kAppNexusPktDropProb = 0.0;
static constexpr size_t kAppEventLoopMs = 200;
static constexpr uint8_t kAppClientAppTid = 100;
static constexpr uint8_t kAppServerAppTid = 200;
static constexpr uint8_t kAppReqType = 3;
static constexpr size_t kAppMinMsgSize =
    Rpc<IBTransport>::max_data_per_pkt() + 1; /* At least 2 packets */

/* Shared between client and server thread */
std::atomic<bool> server_ready; /* Client starts after server is ready */
std::atomic<bool> client_done;  /* Server ends after client is done */

const uint8_t phy_port = 0;
const size_t numa_node = 0;
char local_hostname[kMaxHostnameLen];

struct app_context_t {
  bool is_client;
  Rpc<IBTransport> *rpc;

  size_t num_sm_connect_resps = 0; /* Client-only */
  size_t num_rpc_resps = 0;        /* Client-only */
};

/// The common request handler for all subtests. Copies the request string to
/// the response.
void req_handler(const MsgBuffer *req_msgbuf, app_resp_t *app_resp,
                 void *_context) {
  ASSERT_NE(req_msgbuf, nullptr);
  ASSERT_NE(app_resp, nullptr);
  ASSERT_NE(_context, nullptr);

  auto *context = (app_context_t *)_context;
  ASSERT_FALSE(context->is_client);

  size_t req_size = req_msgbuf->get_data_size();
  test_printf("Server: Received request of length %zu\n", req_size);

  app_resp->prealloc_used = false;
  app_resp->resp_size = req_size;
  app_resp->dyn_resp_msgbuf = context->rpc->alloc_msg_buffer(req_size);
  ASSERT_NE(app_resp->dyn_resp_msgbuf.buf, nullptr);

  memcpy((char *)app_resp->dyn_resp_msgbuf.buf, (char *)req_msgbuf->buf,
         req_size);
  app_resp->resp_size = req_size;
}

/// The common response handler for all subtests. This checks that the request
/// buffer is identical to the response buffer, and increments the number of
/// responses in the context.
void resp_handler(const MsgBuffer *req_msgbuf, const MsgBuffer *resp_msgbuf,
                  void *_context) {
  ASSERT_NE(req_msgbuf, nullptr);
  ASSERT_NE(resp_msgbuf, nullptr);
  ASSERT_NE(_context, nullptr);

  test_printf("Client: Received response of length %zu (request's was %zu)\n",
              (char *)resp_msgbuf->get_data_size(),
              (char *)req_msgbuf->get_data_size());

  ASSERT_EQ(req_msgbuf->get_data_size(), resp_msgbuf->get_data_size());
  ASSERT_STREQ((char *)req_msgbuf->buf, (char *)resp_msgbuf->buf);

  auto *context = (app_context_t *)_context;
  ASSERT_TRUE(context->is_client);
  context->num_rpc_resps++;
}

void sm_hander(Session *session, SessionMgmtEventType sm_event_type,
               SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session);

  auto *context = (app_context_t *)_context;
  ASSERT_TRUE(context->is_client);
  context->num_sm_connect_resps++;

  ASSERT_EQ(sm_err_type, SessionMgmtErrType::kNoError);
  ASSERT_TRUE(sm_event_type == SessionMgmtEventType::kConnected ||
              sm_event_type == SessionMgmtEventType::kDisconnected);
}

/// The server thread used for all subtests
void server_thread_func(Nexus *nexus, uint8_t app_tid) {
  app_context_t context;
  context.is_client = false;

  Rpc<IBTransport> rpc(nexus, (void *)&context, app_tid, &sm_hander, phy_port,
                       numa_node);
  rpc.register_ops(kAppReqType, Ops(req_handler, resp_handler));
  context.rpc = &rpc;
  server_ready = true;

  while (!client_done) { /* Wait for the client */
    rpc.run_event_loop_timeout(kAppEventLoopMs);
  }

  /* The client is done after disconnecting */
  ASSERT_EQ(rpc.num_active_sessions(), 0);
}

/// Test: Send one large request packet and check that we receive the
/// correct response
void one_large_rpc(Nexus *nexus) {
  while (!server_ready) { /* Wait for server */
    usleep(1);
  }

  volatile app_context_t context;
  context.is_client = true;

  Rpc<IBTransport> rpc(nexus, (void *)&context, kAppClientAppTid, &sm_hander,
                       phy_port, numa_node);
  rpc.register_ops(kAppReqType, Ops(req_handler, resp_handler));

  context.rpc = &rpc;

  /* Connect the session */
  Session *session =
      rpc.create_session(local_hostname, kAppServerAppTid, phy_port);

  while (context.num_sm_connect_resps == 0) {
    rpc.run_event_loop_one();
  }
  ASSERT_EQ(context.num_sm_connect_resps, 1);
  ASSERT_EQ(session->state, SessionState::kConnected);

  /* Send a message */
  size_t req_size = kAppMinMsgSize;
  MsgBuffer req_msgbuf = rpc.alloc_msg_buffer(req_size);
  ASSERT_NE(req_msgbuf.buf, nullptr);

  for (size_t i = 0; i < req_size; i++) {
    req_msgbuf.buf[i] = 'a';
  }
  req_msgbuf.buf[req_size - 1] = 0;

  test_printf("test: Sending request of size %zu\n", req_size);
  int ret = rpc.send_request(session, kAppReqType, &req_msgbuf);
  if (ret != 0) {
    test_printf("test: send_request error %s\n",
                rpc.rpc_datapath_err_code_str(ret).c_str());
  }
  ASSERT_EQ(ret, 0);

  rpc.run_event_loop_timeout(kAppEventLoopMs);
  ASSERT_EQ(context.num_rpc_resps, 1);

  rpc.free_msg_buffer(req_msgbuf);

  /* Disconnect the session */
  rpc.destroy_session(session);
  rpc.run_event_loop_timeout(kAppEventLoopMs);

  client_done = true;
}

TEST(OneLargeRpc, OneLargeRpc) {
  Nexus nexus(kAppNexusUdpPort, kAppNexusPktDropProb);
  server_ready = false;
  client_done = false;

  std::thread server_thread(server_thread_func, &nexus, kAppServerAppTid);
  std::thread client_thread(one_large_rpc, &nexus);
  server_thread.join();
  client_thread.join();
}

int main(int argc, char **argv) {
  Nexus::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
