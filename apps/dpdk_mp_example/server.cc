#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include "rpc.h"

DEFINE_string(server_name, "", "Server hostname or IP address");
DEFINE_uint64(server_port, SIZE_MAX, "UDP port for this server to listen on");

static constexpr uint8_t kReqType = 2;
void sm_handler(int, erpc::SmEventType, erpc::SmErrType, void *) {}

erpc::Rpc<erpc::CTransport> *rpc;

void req_handler(erpc::ReqHandle *req_handle, void *) {
  auto &resp = req_handle->pre_resp_msgbuf;
  const std::string resp_str = "Hello from server " + FLAGS_server_name + ":" +
                               std::to_string(FLAGS_server_port);
  rpc->resize_msg_buffer(&resp, resp_str.length() + 1);
  snprintf(reinterpret_cast<char *>(resp.buf), resp_str.size() + 1, "%s",
           resp_str.c_str());

  rpc->enqueue_response(req_handle, &resp);
}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_server_port != SIZE_MAX,
                  "Must specify server's UDP port");
  erpc::rt_assert(FLAGS_server_name.length() != 0,
                  "Must specify server's hostname");

  printf("Server process: Listening on port %zu at %s\n", FLAGS_server_port,
         FLAGS_server_name.c_str());

  const std::string server_uri =
      FLAGS_server_name + ":" + std::to_string(FLAGS_server_port);
  erpc::Nexus nexus(server_uri, 0, 0);
  nexus.register_req_func(kReqType, req_handler);

  rpc = new erpc::Rpc<erpc::CTransport>(&nexus, nullptr, 0, nullptr);
  rpc->run_event_loop(100000);

  delete rpc;
}
