#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include "rpc.h"

static constexpr uint8_t kReqType = 2;
static constexpr size_t kMaxMsgSize = 128;
static constexpr size_t kClientUDPPort = 31850;

DEFINE_string(client_name, "", "Client hostname or IP address");
DEFINE_string(server_name, "", "Client hostname or IP address");
DEFINE_uint64(
    server_0_port, SIZE_MAX,
    "Client-only flag: Management UDP port of the first server process");
DEFINE_uint64(
    server_1_port, SIZE_MAX,
    "Client-only flag: Management UDP port of the second server process");

erpc::Rpc<erpc::CTransport> *rpc;
erpc::MsgBuffer req_0, req_1;
erpc::MsgBuffer resp_0, resp_1;

void cont_func(void *, void *tag) {
  auto *resp = reinterpret_cast<erpc::MsgBuffer *>(tag);
  printf("Response: %s\n", reinterpret_cast<char *>(resp->buf));
}

void sm_handler(int, erpc::SmEventType, erpc::SmErrType, void *) {}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_server_0_port != SIZE_MAX,
                  "Must specify server #0's port");
  erpc::rt_assert(FLAGS_server_1_port != SIZE_MAX,
                  "Must specify server #1's port");
  erpc::rt_assert(FLAGS_client_name.length() != 0,
                  "Must specify client's hostname");
  erpc::rt_assert(FLAGS_server_name.length() != 0,
                  "Must specify server's hostname");

  std::string client_uri =
      FLAGS_client_name + ":" + std::to_string(kClientUDPPort);
  erpc::Nexus nexus(client_uri, 0, 0);

  rpc = new erpc::Rpc<erpc::CTransport>(&nexus, nullptr, 0, sm_handler);

  const std::string server_0_uri =
      FLAGS_server_name + ":" + std::to_string(FLAGS_server_0_port);
  const std::string server_1_uri =
      FLAGS_server_name + ":" + std::to_string(FLAGS_server_1_port);

  const int session_num_0 = rpc->create_session(server_0_uri, 0);
  const int session_num_1 = rpc->create_session(server_1_uri, 0);

  while (!rpc->is_connected(session_num_0)) rpc->run_event_loop_once();
  while (!rpc->is_connected(session_num_1)) rpc->run_event_loop_once();

  req_0 = rpc->alloc_msg_buffer_or_die(kMaxMsgSize);
  resp_0 = rpc->alloc_msg_buffer_or_die(kMaxMsgSize);

  req_1 = rpc->alloc_msg_buffer_or_die(kMaxMsgSize);
  resp_1 = rpc->alloc_msg_buffer_or_die(kMaxMsgSize);

  rpc->enqueue_request(session_num_0, kReqType, &req_0, &resp_0, cont_func,
                       &resp_0 /* tag */);
  rpc->enqueue_request(session_num_1, kReqType, &req_1, &resp_1, cont_func,
                       &resp_1 /* tag */);
  rpc->run_event_loop(100);

  delete rpc;
}
