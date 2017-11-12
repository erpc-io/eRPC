#include "common.h"
Rpc<IBTransport> *rpc;

void req_handler(erpc::ReqHandle *req_handle, void *) {
  auto &resp = req_handle->pre_resp_msgbuf;
  rpc->resize_msg_buffer(&resp, 4);
  sprintf(reinterpret_cast<char *>(resp.buf), "nsdi");

  req_handle->prealloc_used = true;
  rpc->enqueue_response(req_handle);
}

int main() {
  Nexus nexus("128.110.96.136", UDP_PORT);
  nexus.register_req_func(REQ_TYPE, ReqFunc(req_handler, kForeground));

  rpc = new Rpc<IBTransport>(&nexus, nullptr, SERVER_ID, nullptr);
  rpc->run_event_loop(100000);
}
