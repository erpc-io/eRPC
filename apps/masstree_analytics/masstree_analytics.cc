#include "masstree_analytics.h"
#include <signal.h>
#include <cstring>
#include "cityhash/city.h"
#include "util/autorun_helpers.h"

// The keys in the index are 64-bit hashes of keys {0, ..., FLAGS_num_keys}.
// This gives us random-ish 64-bit keys, without requiring actually maintaining
// the set of inserted keys
size_t get_random_key(AppContext *c) {
  size_t _generator_key = c->fastrand.next_u32() % FLAGS_num_keys;
  return CityHash64(reinterpret_cast<const char *>(&_generator_key),
                    sizeof(size_t));
}

void app_cont_func(erpc::RespHandle *, void *, size_t);  // Forward declaration

void point_req_handler(erpc::ReqHandle *req_handle, void *_context) {
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
  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                 sizeof(resp_t));
  auto *resp = reinterpret_cast<resp_t *>(req_handle->pre_resp_msgbuf.buf);

  resp->resp_type = success ? RespType::kFound : RespType::kNotFound;
  resp->value = value;  // Garbage is OK in case of kNotFound

  c->rpc->enqueue_response(req_handle);
}

void range_req_handler(erpc::ReqHandle *req_handle, void *_context) {
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
  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
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

  if (c->fastrand.next_u32() % 100 < FLAGS_range_req_percent) {
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
  *reinterpret_cast<req_t *>(req_msgbuf.buf) = req;

  if (kAppVerbose) {
    printf("masstree_analytics: Trying to send request with msgbuf_idx %zu.\n",
           msgbuf_idx);
  }

  c->client.req_ts[msgbuf_idx] = erpc::rdtsc();
  int ret = c->rpc->enqueue_request(0, req.req_type, &req_msgbuf,
                                    &c->client.resp_msgbuf[msgbuf_idx],
                                    app_cont_func, msgbuf_idx);
  erpc::rt_assert(ret == 0, "Failed to enqueue_request()");
}

void app_cont_func(erpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  size_t msgbuf_idx = _tag;
  if (kAppVerbose) {
    printf("masstree_analytics: Received response for msgbuf %zu.\n",
           msgbuf_idx);
  }

  auto *c = static_cast<AppContext *>(_context);

  const auto *resp_msgbuf = resp_handle->get_resp_msgbuf();
  erpc::rt_assert(resp_msgbuf->get_data_size() == sizeof(resp_t),
                  "Invalid response size");
  c->rpc->release_response(resp_handle);

  assert(resp_msgbuf->get_data_size() > 0);  // Check that the Rpc succeeded

  double usec = erpc::to_usec(erpc::rdtsc() - c->client.req_ts[msgbuf_idx],
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

  constexpr size_t kMeasurement = 200000;
  if (c->client.num_resps_tot++ == kMeasurement) {
    double point_us_median = c->client.point_latency.perc(.5) / 10.0;
    double point_us_99 = c->client.point_latency.perc(.99) / 10.0;
    double range_us_90 = c->client.range_latency.perc(.99);

    double seconds = erpc::sec_since(c->client.tput_t0);
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

void client_thread_func(size_t thread_id, erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id = thread_id;

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, kAppPhyPort);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  // Each client creates a session to only one server thread
  auto server_hostname = erpc::get_hostname_for_process(0);
  auto server_udp_str = erpc::get_udp_port_for_process(0);
  size_t client_gid = (FLAGS_process_id * FLAGS_num_client_threads) + thread_id;
  size_t server_tid = client_gid % FLAGS_num_server_fg_threads;  // eRPC TID

  c.session_num_vec.resize(1);
  c.session_num_vec[0] =
      rpc.create_session(server_hostname + ":" + server_udp_str, server_tid);
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

void server_thread_func(size_t thread_id, erpc::Nexus *nexus, MtIndex *mti,
                        threadinfo_t **ti_arr) {
  AppContext c;
  c.thread_id = thread_id;
  c.server.mt_index = mti;
  c.server.ti_arr = ti_arr;

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, kAppPhyPort);
  c.rpc = &rpc;
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

  std::string hostname = erpc::get_hostname_for_process(FLAGS_process_id);
  std::string udp_port_str = erpc::get_udp_port_for_process(FLAGS_process_id);

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
    erpc::Nexus nexus(hostname + ":" + udp_port_str, kAppEPid, kAppNumaNode,
                      FLAGS_num_server_bg_threads);

    nexus.register_req_func(
        kAppPointReqType,
        erpc::ReqFunc(point_req_handler, erpc::ReqFuncType::kForeground));

    auto range_handler_type = FLAGS_num_server_bg_threads > 0
                                  ? erpc::ReqFuncType::kForeground
                                  : erpc::ReqFuncType::kBackground;
    nexus.register_req_func(
        kAppRangeReqType, erpc::ReqFunc(range_req_handler, range_handler_type));

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
    erpc::Nexus nexus(hostname + ":" + udp_port_str, kAppEPid, kAppNumaNode,
                      FLAGS_num_server_bg_threads);

    std::vector<std::thread> thread_arr(FLAGS_num_client_threads);
    for (size_t i = 0; i < FLAGS_num_client_threads; i++) {
      thread_arr[i] = std::thread(client_thread_func, i, &nexus);
      erpc::bind_to_core(thread_arr[i], FLAGS_numa_node, i);
    }

    for (auto &thread : thread_arr) thread.join();
  }
}
