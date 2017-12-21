#include "common.h"
Rpc<CTransport> *rpc;

void cont_func(erpc::RespHandle *resp_handle, void *, size_t) {
  auto *resp_msgbuf = resp_handle->get_resp_msgbuf();
  printf("%s\n", resp_msgbuf->buf);
  rpc->release_response(resp_handle);

  exit(0);
}

void sm_handler(int, SmEventType, SmErrType, void *) {}

int main() {
  Nexus nexus("10.100.3.16", UDP_PORT);
  rpc = new Rpc<CTransport>(&nexus, nullptr, CLIENT_ID, sm_handler);

  int session_num = rpc->create_session("10.100.3.13", SERVER_ID);
  while (!rpc->is_connected(session_num)) rpc->run_event_loop_once();

  auto req = rpc->alloc_msg_buffer(4);
  auto resp = rpc->alloc_msg_buffer(4);

  rpc->enqueue_request(session_num, REQ_TYPE, &req, &resp, cont_func, 0);
  rpc->run_event_loop(100000);
}
