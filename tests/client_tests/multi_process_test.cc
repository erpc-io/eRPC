#include "client_tests.h"

std::atomic<size_t> num_processes_ready;

void req_handler(erpc::ReqHandle *req_handle, void *_c) {
  auto *c = reinterpret_cast<BasicAppContext *>(_c);

  auto &resp = req_handle->pre_resp_msgbuf;
  c->rpc->resize_msg_buffer(&resp, sizeof(size_t));
  req_handle->prealloc_used = true;
  c->rpc->enqueue_response(req_handle);
}

void cont_func(void *_c, size_t) {
  auto *c = static_cast<BasicAppContext *>(_c);
  c->num_rpc_resps++;
}

void sm_handler(int, erpc::SmEventType, erpc::SmErrType, void *) {}

// This threads acts as a process with a Nexus
void process_proxy_thread_func(size_t process_id, size_t num_processes) {
  auto uri =
      std::string("localhost:") + std::to_string(kBaseSmUdpPort + process_id);
  Nexus nexus(uri, 0, 0);
  nexus.register_req_func(kTestReqType, req_handler);

  BasicAppContext c;
  Rpc<CTransport> rpc(&nexus, &c, 0, sm_handler);
  c.rpc = &rpc;

  c.session_num_arr = new int[num_processes];
  c.req_msgbufs.resize(num_processes);
  c.resp_msgbufs.resize(num_processes);
  for (size_t i = 0; i < num_processes; i++) {
    if (i == process_id) continue;

    auto remote_uri =
        std::string("localhost:") + std::to_string(kBaseSmUdpPort + i);
    c.session_num_arr[i] = c.rpc->create_session(remote_uri, 0);

    c.req_msgbufs[i] = c.rpc->alloc_msg_buffer_or_die(sizeof(size_t));
    c.resp_msgbufs[i] = c.rpc->alloc_msg_buffer_or_die(sizeof(size_t));
  }

  // Barrier
  num_processes_ready++;
  while (num_processes_ready != num_processes_ready) {
  }

  c.rpc->run_event_loop(kTestEventLoopMs);
  assert(c.num_sm_resps == num_processes - 1);

  for (size_t i = 0; i < num_processes; i++) {
    if (i == process_id) continue;

    c.rpc->enqueue_request(c.session_num_arr[i], kTestReqType,
                           &c.req_msgbufs[i], &c.resp_msgbufs[i], cont_func, 0);
  }
  c.rpc->run_event_loop(kTestEventLoopMs);
  assert(c.num_rpc_resps == num_processes - 1);
}

TEST(MultiProcessTest, TwoProcesses) {
  num_processes_ready = 0;
  std::thread process_1_thread(process_proxy_thread_func, 0, 2);
  std::thread process_2_thread(process_proxy_thread_func, 1, 2);

  process_1_thread.join();
  process_2_thread.join();
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
