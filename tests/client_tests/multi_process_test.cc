#include "client_tests.h"

std::atomic<size_t> num_processes_ready;

void req_handler(erpc::ReqHandle *req_handle, void *_c) {
  auto *c = reinterpret_cast<BasicAppContext *>(_c);

  auto &resp = req_handle->pre_resp_msgbuf_;
  c->rpc_->resize_msg_buffer(&resp, sizeof(size_t));
  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void cont_func(void *_c, void *) {
  auto *c = static_cast<BasicAppContext *>(_c);
  c->num_rpc_resps_++;
}

// This threads acts as a process with a Nexus
void process_proxy_thread_func(size_t process_id, size_t num_processes) {
  auto uri = "127.0.0.1:" + std::to_string(kBaseSmUdpPort + process_id);
  Nexus nexus(uri, 0, 0);
  nexus.register_req_func(kTestReqType, req_handler);

  BasicAppContext c;
  Rpc<CTransport> rpc(&nexus, &c, 0, basic_sm_handler);
  c.rpc_ = &rpc;

  // Barrier
  num_processes_ready++;
  while (num_processes_ready != num_processes) {
    // Wait for Rpc objects to be registered with their Nexus
  }

  c.session_num_arr_ = new int[num_processes];
  c.req_msgbufs_.resize(num_processes);
  c.resp_msgbufs_.resize(num_processes);
  for (size_t i = 0; i < num_processes; i++) {
    if (i == process_id) continue;

    auto remote_uri = "127.0.0.1:" + std::to_string(kBaseSmUdpPort + i);
    c.session_num_arr_[i] = c.rpc_->create_session(remote_uri, 0);

    c.req_msgbufs_[i] = c.rpc_->alloc_msg_buffer_or_die(sizeof(size_t));
    c.resp_msgbufs_[i] = c.rpc_->alloc_msg_buffer_or_die(sizeof(size_t));
  }

  wait_for_sm_resps_or_timeout(c, num_processes - 1);
  assert(c.num_sm_resps_ == num_processes - 1);
  printf("Process %zu: All sessions connected\n", process_id);

  for (size_t i = 0; i < num_processes; i++) {
    if (i == process_id) continue;

    c.rpc_->enqueue_request(c.session_num_arr_[i], kTestReqType,
                           &c.req_msgbufs_[i], &c.resp_msgbufs_[i], cont_func,
                           nullptr);
  }

  wait_for_rpc_resps_or_timeout(c, num_processes - 1);
  assert(c.num_rpc_resps_ == num_processes - 1);
}

TEST(MultiProcessTest, TwoProcesses) {
  num_processes_ready = 0;
  std::thread process_proxy_thread_thread_1(process_proxy_thread_func, 0, 2);
  std::thread process_proxy_thread_thread_2(process_proxy_thread_func, 1, 2);

  process_proxy_thread_thread_1.join();
  process_proxy_thread_thread_2.join();
}

TEST(MultiProcessTest, MaxProcesses) {
  num_processes_ready = 0;
  const size_t num_processes = kMaxNumERpcProcesses;

  std::vector<std::thread> process_proxy_thread_vec(num_processes);

  for (size_t i = 0; i < num_processes; i++) {
    process_proxy_thread_vec[i] =
        std::thread(process_proxy_thread_func, i, num_processes);
  }

  for (size_t i = 0; i < num_processes; i++) {
    process_proxy_thread_vec[i].join();
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
