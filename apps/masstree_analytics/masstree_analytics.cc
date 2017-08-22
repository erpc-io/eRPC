#include "masstree_analytics.h"
#include <signal.h>
#include <cstring>

static constexpr bool kAppVerbose = false;

void app_cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration

// Send one request using this MsgBuffer
void send_req(AppContext *c, size_t msgbuf_idx) {
  assert(c != nullptr);

  ERpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == sizeof(size_t));

  if (kAppVerbose) {
    printf("masstree_analytics: Trying to send request with msgbuf_idx %zu.\n",
           msgbuf_idx);
  }

  // Timestamp before trying enqueue_request(). If enqueue_request() fails,
  // we'll timestamp again on the next try.
  c->req_ts[msgbuf_idx] = ERpc::rdtsc();
  int ret = c->rpc->enqueue_request(0, kAppPointReqType, &req_msgbuf,
                                    &c->resp_msgbuf[msgbuf_idx], app_cont_func,
                                    msgbuf_idx);
  _unused(ret);
  assert(ret == 0);
}

void point_req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *c = static_cast<AppContext *>(_context);

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  _unused(req_msgbuf);

  req_handle->prealloc_used = true;
  ERpc::Rpc<ERpc::IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                  sizeof(size_t));
  c->rpc->enqueue_response(req_handle);
}

void range_req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *c = static_cast<AppContext *>(_context);

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  _unused(req_msgbuf);

  req_handle->prealloc_used = true;
  ERpc::Rpc<ERpc::IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                  sizeof(size_t));
  c->rpc->enqueue_response(req_handle);
}

void app_cont_func(ERpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  const ERpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf != nullptr);

  size_t msgbuf_idx = _tag;
  if (kAppVerbose) {
    printf("large_rpc_tput: Received response for msgbuf %zu.\n", msgbuf_idx);
  }

  // Measure latency. 1 us granularity is sufficient for large RPC latency.
  // XXX: Fix.
  auto *c = static_cast<AppContext *>(_context);
  double usec = ERpc::to_usec(ERpc::rdtsc() - c->req_ts[msgbuf_idx],
                              c->rpc->get_freq_ghz());
  assert(usec >= 0);
  c->latency.update(static_cast<size_t>(usec));

  ERpc::rt_assert(resp_msgbuf->get_data_size() == sizeof(size_t),
                  "Invalid response size");

  send_req(c, msgbuf_idx);
}

void client_thread_func(size_t thread_id,
                        ERpc::Nexus<ERpc::IBTransport> *nexus) {
  assert(FLAGS_machine_id > 0);

  AppContext c;
  c.thread_id = thread_id;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&c),
                                   static_cast<uint8_t>(thread_id),
                                   basic_sm_handler, kAppPhyPort, kAppNumaNode);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  // Each client creates a session to only one server thread
  auto server_hostname = get_hostname_for_machine(0);
  size_t server_thread_id = thread_id % FLAGS_num_server_fg_threads;

  c.session_num_vec.resize(1);
  c.session_num_vec[0] =
      rpc.create_session(server_hostname, server_thread_id, kAppPhyPort);
  assert(c.session_num_vec[0] > 0);

  while (c.num_sm_resps != 1) {
    rpc.run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
  assert(c.rpc->is_connected(c.session_num_vec[0]));
  fprintf(stderr, "Thread %zu: Sessions connected.\n", thread_id);

  alloc_req_resp_msg_buffers(&c);
  for (size_t i = 0; i < FLAGS_req_window; i++) send_req(&c, i);
}

void server_thread_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus,
                        MtIndex *, threadinfo_t **) {
  assert(FLAGS_machine_id == 0);

  AppContext c;
  c.thread_id = thread_id;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&c),
                                   static_cast<uint8_t>(thread_id),
                                   basic_sm_handler, kAppPhyPort, kAppNumaNode);
  while (ctrl_c_pressed == 0) rpc.run_event_loop(200);
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);

  // Work around g++-5's unused variable warning for validators
  _unused(req_window_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (is_server()) {
    // Create the Masstree using the main thread and insert a million keys
    threadinfo_t *ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    MtIndex mti;
    mti.setup(ti);

    for (size_t i = 0; i < FLAGS_num_keys; i++) {
      size_t key = i;
      size_t value = i;
      mti.put(key, value, ti);
    }

    // Create Masstree threadinfo structs for server threads
    size_t total_server_threads =
        FLAGS_num_server_fg_threads + FLAGS_num_server_bg_threads;
    threadinfo_t *ti_arr[total_server_threads];

    for (size_t i = 0; i < total_server_threads; i++) {
      ti_arr[i] = threadinfo::make(threadinfo::TI_PROCESS, i);
    }

    // ERpc stuff
    std::string machine_name = get_hostname_for_machine(0);
    ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort,
                                         FLAGS_num_server_bg_threads);

    nexus.register_req_func(
        kAppPointReqType,
        ERpc::ReqFunc(point_req_handler, ERpc::ReqFuncType::kForeground));
    nexus.register_req_func(
        kAppRangeReqType,
        ERpc::ReqFunc(range_req_handler, ERpc::ReqFuncType::kBackground));

    std::thread threads[FLAGS_num_server_fg_threads];
    for (size_t i = 0; i < FLAGS_num_server_fg_threads; i++) {
      threads[i] = std::thread(server_thread_func, i, &nexus, &mti,
                               static_cast<threadinfo_t **>(ti_arr));
      ERpc::bind_to_core(threads[i], i);
    }

    for (size_t i = 0; i < FLAGS_num_server_fg_threads; i++) {
      threads[i].join();
    }
  } else {
    std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
    ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);

    std::thread threads[FLAGS_num_client_threads];
    for (size_t i = 0; i < FLAGS_num_client_threads; i++) {
      threads[i] = std::thread(client_thread_func, i, &nexus);
      ERpc::bind_to_core(threads[i], i);
    }

    for (size_t i = 0; i < FLAGS_num_client_threads; i++) {
      threads[i].join();
    }
  }
}
