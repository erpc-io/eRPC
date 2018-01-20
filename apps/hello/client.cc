#include "common.h"
erpc::Rpc<erpc::CTransport> *rpc;

void cont_func(erpc::RespHandle *resp_handle, void *, size_t) {
  auto *resp_msgbuf = resp_handle->get_resp_msgbuf();
  printf("%s\n", resp_msgbuf->buf);
  rpc->release_response(resp_handle);
}

void sm_handler(int, erpc::SmEventType, erpc::SmErrType, void *) {}

int main() {
  std::string client_uri = kClientHostname + ":" + std::to_string(kUDPPort);
  erpc::Nexus nexus(client_uri, 0, 0);

  rpc = new erpc::Rpc<erpc::CTransport>(&nexus, nullptr, 0, sm_handler);

  std::string server_uri = kServerHostname + ":" + std::to_string(kUDPPort);
  int session_num = rpc->create_session(server_uri, 0);

  while (!rpc->is_connected(session_num)) rpc->run_event_loop_once();

  printf("Message size = %zu\n", kMsgSize);
  auto req = rpc->alloc_msg_buffer(kMsgSize);
  auto resp = rpc->alloc_msg_buffer(kMsgSize);

  rpc->enqueue_request(session_num, kReqType, &req, &resp, cont_func, 0);
  rpc->run_event_loop(100);

  delete rpc;
}
