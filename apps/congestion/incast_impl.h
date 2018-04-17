/**
 * @file incast_impl.h
 * @brief The incast traffic component
 */
#ifndef INCAST_IMPL_H
#define INCAST_IMPL_H

#include "congestion.h"

void connect_sessions_func_incast(AppContext *c) {
  erpc::rt_assert(FLAGS_process_id != 0);
  erpc::rt_assert(c->thread_id < FLAGS_incast_threads_other);

  size_t rem_tid =
      (FLAGS_process_id * FLAGS_incast_threads_other + c->thread_id) %
      FLAGS_incast_threads_zero;

  c->session_num_vec.resize(1);

  printf("congestion: Incast thread %zu: Creating 1 session to 0/%zu.\n",
         c->thread_id, rem_tid);

  c->session_num_vec[0] =
      c->rpc->create_session(erpc::get_uri_for_process(0), rem_tid);
  erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session() failed");

  while (c->num_sm_resps != c->session_num_vec.size()) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  if (FLAGS_incast_throttle != 0.0) {
    erpc::Timely *timely_0 = c->rpc->get_timely(c->session_num_vec[0]);
    double num_flows = (FLAGS_num_processes - 1) * FLAGS_incast_threads_other;
    double fair_share = erpc::kBandwidth / num_flows;
    timely_0->rate = fair_share * FLAGS_incast_throttle;
  }
}

void cont_incast(erpc::RespHandle *, void *, size_t);  // Forward declaration

// Send an incast request using this MsgBuffer
void send_req_incast(AppContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == FLAGS_incast_req_size);

  if (kAppVerbose) {
    printf("congestion: Thread %zu sending incast req using msgbuf_idx %zu.\n",
           c->thread_id, msgbuf_idx);
  }

  c->rpc->enqueue_request(c->session_num_vec[0], kAppReqTypeIncast, &req_msgbuf,
                          &c->resp_msgbuf[msgbuf_idx], cont_incast, msgbuf_idx);

  c->incast_tx_bytes += FLAGS_incast_req_size;
}

// Request handler for incast traffic
void req_handler_incast(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);

  req_handle->prealloc_used = false;
  erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf;
  resp_msgbuf = c->rpc->alloc_msg_buffer(FLAGS_incast_resp_size);
  assert(resp_msgbuf.buf != nullptr);

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  resp_msgbuf.buf[0] = req_msgbuf->buf[0];  // Touch the response
  c->rpc->enqueue_response(req_handle);
}

// Continuation for incast traffic
void cont_incast(erpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  const erpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  size_t msgbuf_idx = _tag;
  if (kAppVerbose) {
    printf("congestion: Received response for msgbuf %zu.\n", msgbuf_idx);
  }

  auto *c = static_cast<AppContext *>(_context);
  assert(resp_msgbuf->get_data_size() == FLAGS_incast_resp_size);
  erpc::rt_assert(resp_msgbuf->buf[0] == kAppDataByte);  // Touch

  c->rpc->release_response(resp_handle);
  send_req_incast(c, msgbuf_idx);
}

// The function executed by each incast thread at non-zero processes
void thread_func_incast_other(size_t thread_id, app_stats_t *app_stats,
                              erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id = thread_id;
  c.app_stats = app_stats;
  if (thread_id == 0) {
    c.tmp_stat = new TmpStat("rx_gbps tx_gbps re_tx avg_us 99_us");
  }

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  connect_sessions_func_incast(&c);
  printf("congestion: Incast thread %zu: Sessions connected.\n", thread_id);

  c.resp_msgbuf[0] = rpc.alloc_msg_buffer(FLAGS_incast_resp_size);
  erpc::rt_assert(c.resp_msgbuf[0].buf != nullptr, "Alloc failed");
  c.req_msgbuf[0] = rpc.alloc_msg_buffer(FLAGS_incast_req_size);
  erpc::rt_assert(c.req_msgbuf[0].buf != nullptr, "Alloc failed");
  memset(c.req_msgbuf[0].buf, kAppDataByte, FLAGS_incast_req_size);

  send_req_incast(&c, 0);

  clock_gettime(CLOCK_REALTIME, &c.tput_t0);
  for (size_t i = 0; i < FLAGS_test_ms; i += kAppEvLoopMs) {
    rpc.run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) break;
    if (c.session_num_vec.size() == 0) continue;  // No stats to print

    double ns = erpc::ns_since(c.tput_t0);  // Don't rely on kAppEvLoopMs

    // Publish stats
    auto &stats = c.app_stats[c.thread_id];
    stats.incast_gbps = c.incast_tx_bytes * 8 / ns;
    stats.re_tx = c.rpc->get_num_retransmissions(c.session_num_vec[0]);
    assert(stats.regular_50_us == 0);
    assert(stats.regular_99_us == 0);

    // Reset stats for next iteration
    c.incast_tx_bytes = 0;
    c.rpc->reset_num_retransmissions(c.session_num_vec[0]);

    erpc::Timely *timely_0 = c.rpc->get_timely(0);

    printf(
        "congestion: Inacst thread %zu: Tput %.2f Gbps. "
        "Retransmissions %zu. "
        "Session 0 Timely: {{%.1f, %.1f, %.1f} us, %.2f Gbps}. "
        "Credits %zu (best = 32).\n",
        c.thread_id, stats.incast_gbps, stats.re_tx, timely_0->get_rtt_perc(.5),
        timely_0->get_rtt_perc(.9), timely_0->get_rtt_perc(.99),
        timely_0->get_rate_gbps(), erpc::kSessionCredits);

    timely_0->reset_rtt_stats();

    if (c.thread_id == 0) {
      app_stats_t accum_stats;
      for (size_t i = 0;
           i < FLAGS_incast_threads_other + FLAGS_regular_threads_other; i++) {
        accum_stats += c.app_stats[i];
      }
      c.tmp_stat->write(accum_stats.to_string());
    }

    clock_gettime(CLOCK_REALTIME, &c.tput_t0);
  }

  // We don't disconnect sessions
}

// The function executed by each incast thread at process zero
void thread_func_incast_zero(size_t thread_id, erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id = thread_id;

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  for (size_t i = 0; i < FLAGS_test_ms; i += kAppEvLoopMs) {
    rpc.run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) break;
  }
}

#endif
