#include "masstree_analytics.h"
#include <signal.h>
#include <cstring>
#include "cityhash/city.h"

// The keys in the index are 64-bit hashes of keys {0, ..., FLAGS_num_keys}.
// This gives us random-ish 64-bit keys, without requiring actually maintaining
// the set of inserted keys
size_t get_random_key(AppContext *c) {
  size_t _generator_key = c->fastrand.next_u32() % FLAGS_num_keys;
  return CityHash64(reinterpret_cast<const char *>(&_generator_key),
                    sizeof(size_t));
}

void app_cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration

void point_req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);

  // Point request handler runs in a foreground thread
  size_t etid = c->rpc->get_etid();
  assert(etid >= FLAGS_num_server_bg_threads &&
         etid < FLAGS_num_server_bg_threads + FLAGS_num_server_fg_threads);

  if (kAppVerbose) {
    printf(
        "masstree_analytics: Running point_req_handler() in eRPC thread %zu.\n",
        etid);
  }

  MtIndex *mti = c->server.mt_index;
  threadinfo_t *ti = c->server.ti_arr[etid];
  assert(mti != nullptr && ti != nullptr);

  const auto *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(req_t));

  auto *req = reinterpret_cast<const req_t *>(req_msgbuf->buf);
  assert(req->req_type == kAppPointReqType);

  size_t value = 0;
  bool success = mti->get(req->point_req.key, value, ti);

  req_handle->prealloc_used = true;
  ERpc::Rpc<ERpc::IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                  sizeof(resp_t));
  auto *resp = reinterpret_cast<resp_t *>(req_handle->pre_resp_msgbuf.buf);

  resp->resp_type = success ? RespType::kFound : RespType::kNotFound;
  resp->value = value;  // Garbage is OK in case of kNotFound

  c->rpc->enqueue_response(req_handle);
}

void range_req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);

  // Range request handler runs in a background thread
  size_t etid = c->rpc->get_etid();
  assert(etid < FLAGS_num_server_bg_threads);

  if (kAppVerbose) {
    printf(
        "masstree_analytics: Running range_req_handler() in eRPC thread %zu.\n",
        etid);
  }

  MtIndex *mti = c->server.mt_index;
  threadinfo_t *ti = c->server.ti_arr[etid];
  assert(mti != nullptr && ti != nullptr);

  const auto *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(req_t));

  auto *req = reinterpret_cast<const req_t *>(req_msgbuf->buf);
  assert(req->req_type == kAppRangeReqType);

  size_t count =
      mti->sum_in_range(req->range_req.key, req->range_req.range, ti);

  req_handle->prealloc_used = true;
  ERpc::Rpc<ERpc::IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                  sizeof(resp_t));
  auto *resp = reinterpret_cast<resp_t *>(req_handle->pre_resp_msgbuf.buf);
  resp->resp_type = RespType::kFound;
  resp->range_count = count;

  c->rpc->enqueue_response(req_handle);
}

// Helper function for clients
req_t generate_request(AppContext *c) {
  req_t req;
  size_t key = get_random_key(c);

  if (c->fastrand.next_u32() % 100 == 0) {
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
  ERpc::MsgBuffer &req_msgbuf = c->client.req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == sizeof(req_t));

  const req_t req = generate_request(c);
  *reinterpret_cast<req_t *>(req_msgbuf.buf) = req;

  if (kAppVerbose) {
    printf("masstree_analytics: Trying to send request with msgbuf_idx %zu.\n",
           msgbuf_idx);
  }

  c->client.req_ts[msgbuf_idx] = ERpc::rdtsc();
  int ret = c->rpc->enqueue_request(0, req.req_type, &req_msgbuf,
                                    &c->client.resp_msgbuf[msgbuf_idx],
                                    app_cont_func, msgbuf_idx);
  ERpc::rt_assert(ret == 0, "Failed to enqueue_request()");
}

void app_cont_func(ERpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  size_t msgbuf_idx = _tag;
  if (kAppVerbose) {
    printf("masstree_analytics: Received response for msgbuf %zu.\n",
           msgbuf_idx);
  }

  auto *c = static_cast<AppContext *>(_context);

  const auto *resp_msgbuf = resp_handle->get_resp_msgbuf();
  ERpc::rt_assert(resp_msgbuf->get_data_size() == sizeof(resp_t),
                  "Invalid response size");
  c->rpc->release_response(resp_handle);

  assert(resp_msgbuf->get_data_size() > 0);  // Check that the Rpc succeeded

  double usec = ERpc::to_usec(ERpc::rdtsc() - c->client.req_ts[msgbuf_idx],
                              c->rpc->get_freq_ghz());
  assert(usec >= 0);

  req_t *req = reinterpret_cast<req_t *>(c->client.req_msgbuf[msgbuf_idx].buf);
  assert(req->req_type == kAppPointReqType ||
         req->req_type == kAppRangeReqType);

  if (req->req_type == kAppPointReqType) {
    c->client.point_latency.update(static_cast<size_t>(usec * 10.0));  // < 1us
  } else {
    c->client.range_latency.update(static_cast<size_t>(usec));
  }

  constexpr size_t kMeasurement = 1000000;
  if (c->client.num_resps_tot++ == kMeasurement) {
    double point_us_median = c->client.point_latency.perc(.5) / 10.0;
    double point_us_99 = c->client.point_latency.perc(.99) / 10.0;
    double range_us_90 = c->client.range_latency.perc(.99);

    double seconds = ERpc::sec_since(c->client.tput_t0);
    double tput = kMeasurement / (seconds * 1000000);

    printf(
        "masstree_analytics: Client %zu. Tput = %.3f Mrps. "
        "Point latency (us) = {%.2f 50, %.2f 99}. "
        "Range latency (us) = %.2f 90.\n",
        c->thread_id, tput, point_us_median, point_us_99, range_us_90);

    clock_gettime(CLOCK_REALTIME, &c->client.tput_t0);
    c->client.num_resps_tot = 0;
    c->client.point_latency.reset();
    c->client.range_latency.reset();
  }

  send_req(c, msgbuf_idx);
}

void client_thread_func(size_t thread_id, ERpc::Nexus *nexus) {
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
  size_t client_gid = (FLAGS_machine_id * FLAGS_num_client_threads) + thread_id;
  size_t server_tid = client_gid % FLAGS_num_server_fg_threads;  // ERpc TID

  c.session_num_vec.resize(1);
  c.session_num_vec[0] =
      rpc.create_session(server_hostname, server_tid, kAppPhyPort);
  assert(c.session_num_vec[0] >= 0);

  while (c.num_sm_resps != 1) {
    rpc.run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
  assert(c.rpc->is_connected(c.session_num_vec[0]));
  fprintf(stderr,
          "masstree_analytics: Thread %zu: Connected. Sending requests.\n",
          thread_id);

  alloc_req_resp_msg_buffers(&c);
  clock_gettime(CLOCK_REALTIME, &c.client.tput_t0);
  for (size_t i = 0; i < FLAGS_req_window; i++) send_req(&c, i);

  while (ctrl_c_pressed == 0) c.rpc->run_event_loop(200);
}

void server_thread_func(size_t thread_id, ERpc::Nexus *nexus, MtIndex *mti,
                        threadinfo_t **ti_arr) {
  assert(FLAGS_machine_id == 0);

  AppContext c;
  c.thread_id = thread_id;
  c.server.mt_index = mti;
  c.server.ti_arr = ti_arr;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&c),
                                   static_cast<uint8_t>(thread_id),
                                   basic_sm_handler, kAppPhyPort, kAppNumaNode);
  c.rpc = &rpc;
  while (ctrl_c_pressed == 0) rpc.run_event_loop(200);
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);

  // Work around g++-5's unused variable warning for validators
  _unused(req_window_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (is_server()) {
    // Create the Masstree using the main thread and insert keys
    threadinfo_t *ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    MtIndex mti;
    mti.setup(ti);

    for (size_t i = 0; i < FLAGS_num_keys; i++) {
      size_t key = CityHash64(reinterpret_cast<const char *>(&i),
                              sizeof(size_t));
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
    ERpc::Nexus nexus(machine_name, kAppNexusUdpPort,
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
    ERpc::Nexus nexus(machine_name, kAppNexusUdpPort, 0);

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
