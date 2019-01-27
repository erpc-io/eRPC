/**
 * @file regular_impl.h
 * @brief The regular traffic component. Each regular thread keeps
 * FLAGS_regular_concurrency requests outstanding.
 */
#ifndef REGULAR_IMPL_H
#define REGULAR_IMPL_H

#include "congestion.h"

// Create a session to each *peer* regular thread in the cluster
void connect_sessions_func_regular(AppContext *c) {
  assert(FLAGS_process_id != 0);
  assert(c->thread_id >= FLAGS_incast_threads_other);

  c->session_num_vec.resize(FLAGS_num_processes - 2);

  size_t session_idx = 0;
  for (size_t p_i = 1; p_i < FLAGS_num_processes; p_i++) {
    if (p_i == FLAGS_process_id) continue;

    c->session_num_vec[session_idx] =
        c->rpc->create_session(erpc::get_uri_for_process(p_i), c->thread_id);
    erpc::rt_assert(c->session_num_vec[session_idx] >= 0);

    if (kAppVerbose) {
      printf("congestion: Regular thread %zu: Creating session to %zu.\n",
             c->thread_id, p_i);
    }

    session_idx++;
  }
  assert(session_idx == c->session_num_vec.size());

  while (c->num_sm_resps != c->session_num_vec.size()) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
}

void cont_regular(void *, void *);  // Forward declaration

// Send a regular request using this MsgBuffer
void send_req_regular(AppContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == FLAGS_regular_req_size);

  if (kAppVerbose) {
    printf("congestion: Thread %zu sending regular req using msgbuf_idx %zu.\n",
           c->thread_id, msgbuf_idx);
  }

  size_t session_idx = c->fastrand.next_u32() % c->session_num_vec.size();
  c->req_ts[msgbuf_idx] = erpc::rdtsc();
  c->rpc->enqueue_request(c->session_num_vec[session_idx], kAppReqTypeRegular,
                          &req_msgbuf, &c->resp_msgbuf[msgbuf_idx],
                          cont_regular, reinterpret_cast<void *>(msgbuf_idx));
}

// Request handler for regular traffic
void req_handler_regular(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();

  if (FLAGS_regular_resp_size <= erpc::CTransport::kMaxDataPerPkt) {
    erpc::MsgBuffer &resp_msgbuf = req_handle->pre_resp_msgbuf;
    c->rpc->resize_msg_buffer(&resp_msgbuf, FLAGS_regular_resp_size);
    resp_msgbuf.buf[0] = req_msgbuf->buf[0];  // Touch the response
    c->rpc->enqueue_response(req_handle, &resp_msgbuf);
  } else {
    erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf;
    resp_msgbuf = c->rpc->alloc_msg_buffer(FLAGS_regular_resp_size);
    resp_msgbuf.buf[0] = req_msgbuf->buf[0];  // Touch the response
    c->rpc->enqueue_response(req_handle, &resp_msgbuf);
  }
}

// Continuation for regular traffic
void cont_regular(void *_context, void *_msgbuf_idx) {
  auto *c = static_cast<AppContext *>(_context);
  auto msgbuf_idx = reinterpret_cast<size_t>(_msgbuf_idx);

  const erpc::MsgBuffer *resp_msgbuf = &c->resp_msgbuf[msgbuf_idx];
  if (kAppVerbose) {
    printf("congestion: Received regular resp for msgbuf %zu.\n", msgbuf_idx);
  }

  double usec = erpc::to_usec(erpc::rdtsc() - c->req_ts[msgbuf_idx],
                              c->rpc->get_freq_ghz());
  c->regular_latency.update(usec / FLAGS_regular_latency_divisor);

  assert(resp_msgbuf->get_data_size() == FLAGS_regular_resp_size);
  erpc::rt_assert(resp_msgbuf->buf[0] == kAppDataByte);  // Touch

  send_req_regular(c, msgbuf_idx);  // Clock this response
}

// The function executed by each regular thread in the cluster
void thread_func_regular(size_t thread_id, app_stats_t *app_stats,
                         erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id = thread_id;
  c.app_stats = app_stats;
  if (thread_id == 0) c.tmp_stat = new TmpStat(app_stats_t::get_template_str());

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  connect_sessions_func_regular(&c);
  printf("congestion: Regular thread %zu: Sessions connected.\n", thread_id);

  for (size_t i = 0; i < FLAGS_regular_concurrency; i++) {
    c.resp_msgbuf[i] = rpc.alloc_msg_buffer_or_die(FLAGS_regular_resp_size);
    c.req_msgbuf[i] = rpc.alloc_msg_buffer_or_die(FLAGS_regular_req_size);
    memset(c.req_msgbuf[i].buf, kAppDataByte, FLAGS_regular_req_size);
  }

  for (size_t msgbuf_i = 0; msgbuf_i < FLAGS_regular_concurrency; msgbuf_i++) {
    send_req_regular(&c, msgbuf_i);
  }

  for (size_t i = 0; i < FLAGS_test_ms; i += kAppEvLoopMs) {
    rpc.run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) break;
    if (c.session_num_vec.size() == 0) continue;  // No stats to print

    // Publish stats
    auto &stats = c.app_stats[c.thread_id];
    assert(stats.incast_gbps == 0);
    stats.re_tx = c.rpc->pkt_loss_stats.num_re_tx;
    stats.regular_50_us =
        c.regular_latency.perc(0.50) * FLAGS_regular_latency_divisor;
    stats.regular_99_us =
        c.regular_latency.perc(0.99) * FLAGS_regular_latency_divisor;
    stats.regular_999_us =
        c.regular_latency.perc(0.999) * FLAGS_regular_latency_divisor;

    // Reset stats for next iteration
    c.rpc->pkt_loss_stats.num_re_tx = 0;
    c.regular_latency.reset();

    printf(
        "congestion: Regular thread %zu: Retransmissions %zu. "
        "Latency {%.1f, %.1f, %.1f us}.\n",
        c.thread_id, stats.re_tx, stats.regular_50_us, stats.regular_99_us,
        stats.regular_999_us);
    // An incast thread will write to tmp_stat
  }
  // We don't disconnect sessions
}

#endif
