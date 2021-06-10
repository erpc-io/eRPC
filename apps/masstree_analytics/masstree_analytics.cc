#include "masstree_analytics.h"
#include <signal.h>
#include <cstring>
#include "mica/util/cityhash/city.h"
#include "util/autorun_helpers.h"

// The keys in the index are 64-bit hashes of keys {0, ..., FLAGS_num_keys}.
// This gives us random-ish 64-bit keys, without requiring actually maintaining
// the set of inserted keys
size_t get_random_key(AppContext *c) {
  size_t _generator_key = c->fastrand_.next_u32() % FLAGS_num_keys;
  return CityHash64(reinterpret_cast<const char *>(&_generator_key),
                    sizeof(size_t));
}

void app_cont_func(void *, void *);  // Forward declaration

void point_req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);

  // Handler for point requests runs in a foreground thread
  const size_t etid = c->rpc_->get_etid();
  assert(etid >= FLAGS_num_server_bg_threads &&
         etid < FLAGS_num_server_bg_threads + FLAGS_num_server_fg_threads);

  if (kAppVerbose) {
    printf("main: Handling point request in eRPC thread %zu.\n", etid);
  }

  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf_,
                                                 sizeof(resp_t));

  if (kBypassMasstree) {
    // Send a garbage response
    c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
    return;
  }

  MtIndex *mti = c->server.mt_index;
  threadinfo_t *ti = c->server.ti_arr[etid];
  assert(mti != nullptr && ti != nullptr);

  const auto *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(req_t));

  auto *req = reinterpret_cast<const req_t *>(req_msgbuf->buf_);
  assert(req->req_type == kAppPointReqType);

  size_t value = 0;
  bool success = mti->get(req->point_req.key, value, ti);

  auto *resp = reinterpret_cast<resp_t *>(req_handle->pre_resp_msgbuf_.buf_);
  resp->resp_type = success ? RespType::kFound : RespType::kNotFound;
  resp->value = value;  // Garbage is OK in case of kNotFound
  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void range_req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);

  // Range request handler runs in a background thread
  const size_t etid = c->rpc_->get_etid();
  assert(etid < FLAGS_num_server_bg_threads);

  if (kAppVerbose) {
    printf("main: Handling range request in eRPC thread %zu.\n", etid);
  }

  MtIndex *mti = c->server.mt_index;
  threadinfo_t *ti = c->server.ti_arr[etid];
  assert(mti != nullptr && ti != nullptr);

  const auto *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(req_t));

  auto *req = reinterpret_cast<const req_t *>(req_msgbuf->buf_);
  assert(req->req_type == kAppRangeReqType);

  const size_t count =
      mti->sum_in_range(req->range_req.key, req->range_req.range, ti);

  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf_,
                                                 sizeof(resp_t));
  auto *resp = reinterpret_cast<resp_t *>(req_handle->pre_resp_msgbuf_.buf_);
  resp->resp_type = RespType::kFound;
  resp->range_count = count;

  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

// Helper function for clients
req_t generate_request(AppContext *c) {
  req_t req;
  const size_t key = get_random_key(c);

  if (c->fastrand_.next_u32() % 100 < FLAGS_range_req_percent) {
    // Generate a range request
    req.req_type = kAppRangeReqType;
    req.range_req.key = key;
    req.range_req.range = FLAGS_range_size;
  } else {
    // Generate a point request
    req.req_type = kAppPointReqType;
    req.point_req.key = key;
  }

  return req;
}

// Send one request using this MsgBuffer
void send_req(AppContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->client.req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == sizeof(req_t));

  const req_t req = generate_request(c);
  *reinterpret_cast<req_t *>(req_msgbuf.buf_) = req;

  if (kAppVerbose) {
    printf("main: Enqueuing request with msgbuf_idx %zu.\n", msgbuf_idx);
  }

  c->client.req_ts[msgbuf_idx] = erpc::rdtsc();
  c->rpc_->enqueue_request(0, req.req_type, &req_msgbuf,
                           &c->client.resp_msgbuf[msgbuf_idx], app_cont_func,
                           reinterpret_cast<void *>(msgbuf_idx));
}

void app_cont_func(void *_context, void *_msgbuf_idx) {
  auto *c = static_cast<AppContext *>(_context);
  const auto msgbuf_idx = reinterpret_cast<size_t>(_msgbuf_idx);
  if (kAppVerbose) {
    printf("main: Received response for msgbuf %zu.\n", msgbuf_idx);
  }

  const auto &resp_msgbuf = c->client.resp_msgbuf[msgbuf_idx];
  erpc::rt_assert(resp_msgbuf.get_data_size() == sizeof(resp_t),
                  "Invalid response size");

  const double usec = erpc::to_usec(
      erpc::rdtsc() - c->client.req_ts[msgbuf_idx], c->rpc_->get_freq_ghz());
  assert(usec >= 0);

  req_t *req = reinterpret_cast<req_t *>(c->client.req_msgbuf[msgbuf_idx].buf_);
  assert(req->req_type == kAppPointReqType ||
         req->req_type == kAppRangeReqType);

  if (req->req_type == kAppPointReqType) {
    c->client.point_latency.update(static_cast<size_t>(usec * 10.0));  // < 1us
  } else {
    c->client.range_latency.update(static_cast<size_t>(usec));
  }

  c->client.num_resps_tot++;
  send_req(c, msgbuf_idx);
}

void client_print_stats(AppContext &c) {
  const double seconds = erpc::sec_since(c.client.tput_t0);
  const double tput_mrps = c.client.num_resps_tot / (seconds * 1000000);
  app_stats_t &stats = c.client.app_stats[c.thread_id_];
  stats.mrps = tput_mrps;
  stats.lat_us_50 = c.client.point_latency.perc(0.50) / 10.0;
  stats.lat_us_99 = c.client.point_latency.perc(0.99) / 10.0;

  printf(
      "Client %zu. Tput = %.3f Mrps. "
      "Point-query latency (us) = {%.2f 50th, %.2f 99th}. "
      "Range-query latency (us) = {%zu 99th}.\n",
      c.thread_id_, tput_mrps, stats.lat_us_50, stats.lat_us_99,
      c.client.range_latency.perc(.99));

  if (c.thread_id_ == 0) {
    app_stats_t accum;
    for (size_t i = 0; i < fLU64::FLAGS_num_client_threads; i++) {
      accum += c.client.app_stats[i];
    }
    accum.lat_us_50 /= FLAGS_num_client_threads;
    accum.lat_us_99 /= FLAGS_num_client_threads;
    c.tmp_stat_->write(accum.to_string());
  }

  c.client.num_resps_tot = 0;
  c.client.point_latency.reset();
  c.client.range_latency.reset();

  clock_gettime(CLOCK_REALTIME, &c.client.tput_t0);
}

void client_thread_func(size_t thread_id, app_stats_t *app_stats,
                        erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id_ = thread_id;
  c.client.app_stats = app_stats;

  if (thread_id == 0) {
    c.tmp_stat_ = new TmpStat(app_stats_t::get_template_str());
  }

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  const uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);
  rpc.retry_connect_on_invalid_rpc_id_ = true;
  c.rpc_ = &rpc;

  // Each client creates a session to only one server thread
  const size_t client_gid =
      (FLAGS_process_id * FLAGS_num_client_threads) + thread_id;
  const size_t server_tid =
      client_gid % FLAGS_num_server_fg_threads;  // eRPC TID

  c.session_num_vec_.resize(1);
  c.session_num_vec_[0] =
      rpc.create_session(erpc::get_uri_for_process(0), server_tid);
  assert(c.session_num_vec_[0] >= 0);

  while (c.num_sm_resps_ != 1) {
    rpc.run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
  assert(c.rpc_->is_connected(c.session_num_vec_[0]));
  fprintf(stderr, "main: Thread %zu: Connected. Sending requests.\n",
          thread_id);

  alloc_req_resp_msg_buffers(&c);
  clock_gettime(CLOCK_REALTIME, &c.client.tput_t0);
  for (size_t i = 0; i < FLAGS_req_window; i++) send_req(&c, i);

  for (size_t i = 0; i < FLAGS_test_ms; i += kAppEvLoopMs) {
    c.rpc_->run_event_loop(kAppEvLoopMs);
    if (ctrl_c_pressed == 1) break;
    client_print_stats(c);
  }
}

void server_thread_func(size_t thread_id, erpc::Nexus *nexus, MtIndex *mti,
                        threadinfo_t **ti_arr) {
  AppContext c;
  c.thread_id_ = thread_id;
  c.server.mt_index = mti;
  c.server.ti_arr = ti_arr;

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);
  c.rpc_ = &rpc;
  while (ctrl_c_pressed == 0) rpc.run_event_loop(200);
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_req_window <= kAppMaxReqWindow, "Invalid req window");
  erpc::rt_assert(FLAGS_range_req_percent <= 100, "Invalid range req percent");

  if (FLAGS_num_server_bg_threads == 0) {
    printf(
        "main: Warning: No background threads. "
        "Range queries will run in foreground.\n");
  }

  if (is_server()) {
    erpc::rt_assert(FLAGS_process_id == 0, "Invalid server process ID");

    // Create the Masstree using the main thread and insert keys
    threadinfo_t *ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    MtIndex mti;
    mti.setup(ti);

    for (size_t i = 0; i < FLAGS_num_keys; i++) {
      size_t key =
          CityHash64(reinterpret_cast<const char *>(&i), sizeof(size_t));
      size_t value = i;
      mti.put(key, value, ti);
    }

    // Create Masstree threadinfo structs for server threads
    size_t total_server_threads =
        FLAGS_num_server_fg_threads + FLAGS_num_server_bg_threads;
    auto ti_arr = new threadinfo_t *[total_server_threads];

    for (size_t i = 0; i < total_server_threads; i++) {
      ti_arr[i] = threadinfo::make(threadinfo::TI_PROCESS, i);
    }

    // eRPC stuff
    erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                      FLAGS_numa_node, FLAGS_num_server_bg_threads);

    nexus.register_req_func(kAppPointReqType, point_req_handler,
                            erpc::ReqFuncType::kForeground);

    auto range_handler_type = FLAGS_num_server_bg_threads > 0
                                  ? erpc::ReqFuncType::kBackground
                                  : erpc::ReqFuncType::kForeground;
    nexus.register_req_func(kAppRangeReqType, range_req_handler,
                            range_handler_type);

    std::vector<std::thread> thread_arr(FLAGS_num_server_fg_threads);
    for (size_t i = 0; i < FLAGS_num_server_fg_threads; i++) {
      thread_arr[i] = std::thread(server_thread_func, i, &nexus, &mti,
                                  static_cast<threadinfo_t **>(ti_arr));
      erpc::bind_to_core(thread_arr[i], FLAGS_numa_node, i);
    }

    for (auto &thread : thread_arr) thread.join();
    delete[] ti_arr;
  } else {
    erpc::rt_assert(FLAGS_process_id > 0, "Invalid process ID");
    erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                      FLAGS_numa_node, FLAGS_num_server_bg_threads);

    std::vector<std::thread> thread_arr(FLAGS_num_client_threads);
    auto *app_stats = new app_stats_t[FLAGS_num_client_threads];
    for (size_t i = 0; i < FLAGS_num_client_threads; i++) {
      thread_arr[i] = std::thread(client_thread_func, i, app_stats, &nexus);
      erpc::bind_to_core(thread_arr[i], FLAGS_numa_node, i);
    }

    for (auto &thread : thread_arr) thread.join();
  }
}
