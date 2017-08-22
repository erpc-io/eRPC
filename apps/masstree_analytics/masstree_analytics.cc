#include "masstree_analytics.h"
#include <signal.h>
#include <cstring>

static constexpr bool kAppVerbose = false;

void app_cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration

// Send one request using this MsgBuffer
void send_req_one(AppContext *c, size_t msgbuf_idx) {
  assert(c != nullptr);

  size_t session_idx = c->req_vec[i].session_idx;

  ERpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == FLAGS_req_size);

  if (kAppVerbose) {
    printf(
        "large_rpc_tput: Trying to send request for session index %zu, "
        "msgbuf_idx %zu.\n",
        session_idx, msgbuf_idx);
  }

  // Timestamp before trying enqueue_request(). If enqueue_request() fails,
  // we'll timestamp again on the next try.
  c->req_ts[msgbuf_idx] = ERpc::rdtsc();
  int ret = c->rpc->enqueue_request(
      c->session_num_vec[session_idx], kAppReqType, &req_msgbuf,
      &c->resp_msgbuf[msgbuf_idx], app_cont_func, c->req_vec[i]._tag);
  assert(ret == 0 || ret == -EBUSY);

  if (ret == -EBUSY) {
    c->req_vec[write_index] = c->req_vec[i];
    write_index++;
    // Try other requests
  } else {
    c->stat_req_vec[session_idx]++;
    c->stat_tx_bytes_tot += FLAGS_req_size;
  }

  c->req_vec.resize(write_index);  // Pending requests = write_index
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

  size_t msgbuf_idx = static_cast<tag_t>(_tag).msgbuf_idx;
  size_t session_idx = static_cast<tag_t>(_tag).session_idx;
  if (kAppVerbose) {
    printf("large_rpc_tput: Received response for msgbuf %zu, session %zu.\n",
           msgbuf_idx, session_idx);
  }

  // Measure latency. 1 us granularity is sufficient for large RPC latency.
  auto *c = static_cast<AppContext *>(_context);
  double usec = ERpc::to_usec(ERpc::rdtsc() - c->req_ts[msgbuf_idx],
                              c->rpc->get_freq_ghz());
  assert(usec >= 0);
  c->latency.update(static_cast<size_t>(usec));

  // Check the response
  if (unlikely(resp_msgbuf->get_data_size() != FLAGS_resp_size)) {
    throw std::runtime_error("Invalid response size.\n");
  }

  if (kAppMemset) {
    // Check all response cachelines (checking every byte is slow)
    for (size_t i = 0; i < FLAGS_resp_size; i += 64) {
      if (unlikely(resp_msgbuf->buf[i] != kAppDataByte)) {
        throw std::runtime_error("Invalid response data.");
      }
    }
  } else {
    if (unlikely(resp_msgbuf->buf[0] != kAppDataByte)) {
      throw std::runtime_error("Invalid response data.");
    }
  }

  c->stat_rx_bytes_tot += FLAGS_resp_size;
  c->rpc->release_response(resp_handle);

  if (c->stat_rx_bytes_tot >= 50000000 || c->stat_tx_bytes_tot >= 50000000) {
    float ipc = -1.0;
    if (FLAGS_num_threads == 1) ipc = papi_get_ipc();

    double ns = ERpc::ns_since(c->tput_t0);
    double rx_GBps = c->stat_rx_bytes_tot / ns;
    double tx_GBps = c->stat_tx_bytes_tot / ns;
    double avg_us = c->latency.avg();
    double _99_us = c->latency.perc(.99);

    std::string session_req_count_str;
    for (size_t session_req_count : c->stat_req_vec) {
      session_req_count_str += std::to_string(session_req_count);
      session_req_count_str += " ";
    }

    printf(
        "large_rpc_tput: Thread %zu: Response tput: RX %.3f GB/s, "
        "TX %.3f GB/s, avg latency = %.1f us, 99%% latency = %.1f us. "
        "RX = %.3f MB, TX = %.3f MB. IPC = %.3f. Requests on sessions = %s.\n",
        c->thread_id, rx_GBps, tx_GBps, avg_us, _99_us,
        c->stat_rx_bytes_tot / 1000000.0, c->stat_tx_bytes_tot / 1000000.0, ipc,
        session_req_count_str.c_str());

    // Stats: rx_GBps tx_GBps avg_us 99_us
    c->tmp_stat->write(std::to_string(rx_GBps) + " " + std::to_string(tx_GBps) +
                       " " + std::to_string(avg_us) + " " +
                       std::to_string(_99_us));

    c->latency.reset();
    c->stat_rx_bytes_tot = 0;
    c->stat_tx_bytes_tot = 0;
    c->rpc->reset_dpath_stats_st();
    std::fill(c->stat_req_vec.begin(), c->stat_req_vec.end(), 0);

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  // Create a new request clocking this response, and put in request queue
  if (kAppMemset) {
    memset(c->req_msgbuf[msgbuf_idx].buf, kAppDataByte, FLAGS_req_size);
  } else {
    c->req_msgbuf[msgbuf_idx].buf[0] = kAppDataByte;
  }

  // For some profiles, the session_idx argument will be ignored
  c->req_vec.push_back(tag_t(get_session_idx_func(c, session_idx), msgbuf_idx));

  // Try to send the queued requests. The request buffer for these requests is
  // already filled.
  send_reqs(c);
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

  if (FLAGS_machine_id == 0) {
    // Server
    while (ctrl_c_pressed == 0) rpc.run_event_loop(200);
    return;
  }

  // Each client creates a session to only one server thread
  auto server_hostname = get_hostname_for_machine(0);
  size_t server_thread_id = thread_id % FLAGS_num_server_fg_threads;

  c.session_num_vec.resize(1);
  c.session_num_vec[0] =
      rpc.create_session(server_hostname, server_thread_id, kAppPhyPort);

  while (c.num_sm_resps != 1) {
    rpc.run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  fprintf(stderr, "Thread %zu: Sessions connected.\n", thread_id);
  clock_gettime(CLOCK_REALTIME, &c.tput_t0);
}

void server_thread_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus,
                        MtIndex *mti, threadinfo_t *ti) {
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
  _unused(concurrency_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (is_server()) {
    // Create the Masstree using the main thread and insert a million keys
    threadinfo_t *ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    MtIndex mti;
    mti.setup(ti);

    for (size_t i = 0; i < kNumKeys; i++) {
      size_t key = i;
      size_t value = i;
      mti.put(key, value, ti);
    }

    // Create Masstree threadinfo structs for worker threads
    std::vector<threadinfo *> ti_vec;
    for (size_t i = 0; i < kNumWorkerThreads; i++) {
      ti_vec.push_back(threadinfo::make(threadinfo::TI_PROCESS, i));
    }

    // ERpc stuff
    std::string machine_name = get_hostname_for_machine(0);
    ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort,
                                         FLAGS_server_bg_threads);

    nexus.register_req_func(
        kAppReqType,
        ERpc::ReqFunc(point_req_handler, ERpc::ReqFuncType::kForeground));
    nexus.register_req_func(
        kAppReqType,
        ERpc::ReqFunc(range_req_handler, ERpc::ReqFuncType::kBackground));

    std::thread threads[num_threads];
    for (size_t i = 0; i < FLAGS_num_server_fg_threads; i++) {
      threads[i] = std::thread(server_thread_func, i, &nexus, &mti);
      ERpc::bind_to_core(threads[i], i);
    }

    for (size_t i = 0; i < FLAGS_num_server_fg_threads; i++) threads[i].join();
  } else {
    std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
    ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort, 0);

    std::thread threads[FLAGS_num_client_threads];
    for (size_t i = 0; i < FLAGS_num_client_threads; i++) {
      threads[i] = std::thread(client_thread_func, i, &nexus);
      ERpc::bind_to_core(threads[i], i);
    }

    for (size_t i = 0; i < FLAGS_num_client_threads; i++) threads[i].join();
  }
}
