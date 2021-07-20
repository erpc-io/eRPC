#include "masstree_analytics.h"
#include <signal.h>
#include <cstring>
#include "mica/util/cityhash/city.h"
#include "util/autorun_helpers.h"

void app_cont_func(void *, void *);  // Forward declaration

static constexpr bool kAppVerbose = false;

// Generate the key for this key index
void key_gen(size_t index, uint8_t *key) {
  static_assert(MtIndex::kKeySize >= 2 * sizeof(uint64_t), "");
  auto *key_64 = reinterpret_cast<uint64_t *>(key);
  key_64[0] = 10;
  key_64[1] = index * 8192;
}

/// Return the pre-known quantity stored in each 32-bit chunk of the value for
/// the key for this seed
uint32_t get_value32_for_seed(uint32_t seed) { return seed + 1; }

void point_req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);

  // Handler for point requests runs in a foreground thread
  const size_t etid = c->rpc_->get_etid();
  assert(etid >= FLAGS_num_server_bg_threads &&
         etid < FLAGS_num_server_bg_threads + FLAGS_num_server_fg_threads);

  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf_,
                                                 sizeof(wire_resp_t));

  if (kBypassMasstree) {
    // Send a garbage response
    c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
    return;
  }

  MtIndex *mti = c->server.mt_index;
  threadinfo_t *ti = c->server.ti_arr[etid];
  assert(mti != nullptr && ti != nullptr);

  const auto *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == sizeof(wire_req_t));

  auto *req = reinterpret_cast<const wire_req_t *>(req_msgbuf->buf_);
  assert(req->req_type == kAppPointReqType);
  uint8_t key_copy[MtIndex::kKeySize];  // mti->get() modifies key
  memcpy(key_copy, req->point_req.key, MtIndex::kKeySize);

  auto *resp =
      reinterpret_cast<wire_resp_t *>(req_handle->pre_resp_msgbuf_.buf_);
  const bool success = mti->get(key_copy, resp->value, ti);
  resp->resp_type = success ? RespType::kFound : RespType::kNotFound;

  if (kAppVerbose) {
    printf(
        "main: Handled point request in eRPC thread %zu. Key %s, found %s, "
        "value %s\n",
        etid, req->to_string().c_str(), success ? "yes" : "no",
        success ? resp->to_string().c_str() : "N/A");
  }

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
  assert(req_msgbuf->get_data_size() == sizeof(wire_req_t));

  auto *req = reinterpret_cast<const wire_req_t *>(req_msgbuf->buf_);
  assert(req->req_type == kAppRangeReqType);
  uint8_t key_copy[MtIndex::kKeySize];  // mti->sum_in_range() modifies key
  memcpy(key_copy, req->point_req.key, MtIndex::kKeySize);

  const size_t count = mti->sum_in_range(key_copy, req->range_req.range, ti);

  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf_,
                                                 sizeof(wire_resp_t));
  auto *resp =
      reinterpret_cast<wire_resp_t *>(req_handle->pre_resp_msgbuf_.buf_);
  resp->resp_type = RespType::kFound;
  resp->range_count = count;

  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

// Send one request using this MsgBuffer
void send_req(AppContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->client.window_[msgbuf_idx].req_msgbuf_;
  assert(req_msgbuf.get_data_size() == sizeof(wire_req_t));

  // Generate a random request
  wire_req_t req;
  const size_t rand_key_index = c->fastrand_.next_u32() % FLAGS_num_keys;
  key_gen(rand_key_index, req.point_req.key);

  if (c->fastrand_.next_u32() % 100 < FLAGS_range_req_percent) {
    // Generate a range request
    req.req_type = kAppRangeReqType;
    req.range_req.range = FLAGS_range_size;
  } else {
    req.req_type = kAppPointReqType;  // Generate a point request
  }

  *reinterpret_cast<wire_req_t *>(req_msgbuf.buf_) = req;

  c->client.window_[msgbuf_idx].req_seed_ = rand_key_index;
  c->client.window_[msgbuf_idx].req_ts_ = erpc::rdtsc();

  if (kAppVerbose) {
    printf("main: Enqueuing request with msgbuf_idx %zu.\n", msgbuf_idx);
    sleep(1);
  }

  c->rpc_->enqueue_request(0, req.req_type, &req_msgbuf,
                           &c->client.window_[msgbuf_idx].resp_msgbuf_,
                           app_cont_func, reinterpret_cast<void *>(msgbuf_idx));
}

void app_cont_func(void *_context, void *_msgbuf_idx) {
  auto *c = static_cast<AppContext *>(_context);
  const auto msgbuf_idx = reinterpret_cast<size_t>(_msgbuf_idx);
  if (kAppVerbose) {
    printf("main: Received response for msgbuf %zu.\n", msgbuf_idx);
  }

  const auto &resp_msgbuf = c->client.window_[msgbuf_idx].resp_msgbuf_;
  erpc::rt_assert(resp_msgbuf.get_data_size() == sizeof(wire_resp_t),
                  "Invalid response size");

  const double usec =
      erpc::to_usec(erpc::rdtsc() - c->client.window_[msgbuf_idx].req_ts_,
                    c->rpc_->get_freq_ghz());
  assert(usec >= 0);

  const auto *req = reinterpret_cast<wire_req_t *>(
      c->client.window_[msgbuf_idx].req_msgbuf_.buf_);
  assert(req->req_type == kAppPointReqType ||
         req->req_type == kAppRangeReqType);

  if (req->req_type == kAppPointReqType) {
    c->client.point_latency.update(static_cast<size_t>(usec * 10.0));  // < 1us

    // Check the value
    {
      const auto *wire_resp = reinterpret_cast<wire_resp_t *>(resp_msgbuf.buf_);
      const uint32_t recvd_value =
          *reinterpret_cast<const uint32_t *>(wire_resp->value);
      const uint32_t req_seed = c->client.window_[msgbuf_idx].req_seed_;
      if (recvd_value != get_value32_for_seed(req_seed)) {
        fprintf(stderr,
                "main: Value mismatch. Req seed = %u, recvd_value (first four "
                "bytes = %u)\n",
                req_seed, recvd_value);
      }
    }
  } else {
    c->client.range_latency.update(static_cast<size_t>(usec));
  }

  c->client.num_resps_tot++;
  send_req(c, msgbuf_idx);
}

void client_print_stats(AppContext &c) {
  const double seconds = c.client.tput_timer.get_us() / 1e6;
  const double tput_mrps = c.client.num_resps_tot / (seconds * 1000000);
  app_stats_t &stats = c.client.app_stats[c.thread_id_];
  stats.mrps = tput_mrps;
  stats.lat_us_50 = c.client.point_latency.perc(0.50) / 10.0;
  stats.lat_us_90 = c.client.point_latency.perc(0.90) / 10.0;
  stats.lat_us_99 = c.client.point_latency.perc(0.99) / 10.0;

  printf(
      "Client %zu. Tput = %.3f Mrps. "
      "Point-query latency (us) = {%.1f 50th, %.1f 90th, %.1f 99th}. "
      "Range-query latency (us) = {%zu 99th}.\n",
      c.thread_id_, tput_mrps, stats.lat_us_50, stats.lat_us_90,
      stats.lat_us_99, c.client.range_latency.perc(.99));

  if (c.thread_id_ == 0) {
    app_stats_t accum;
    for (size_t i = 0; i < FLAGS_num_client_threads; i++) {
      accum += c.client.app_stats[i];
    }
    accum.lat_us_50 /= FLAGS_num_client_threads;
    accum.lat_us_99 /= FLAGS_num_client_threads;
    c.tmp_stat_->write(accum.to_string());
  }

  c.client.num_resps_tot = 0;
  c.client.point_latency.reset();
  c.client.range_latency.reset();

  c.client.tput_timer.reset();
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
  c.client.tput_timer.reset();
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

/**
 * @brief Populate Masstree in parallel from multiple threads
 *
 * @param thread_id Index of this thread among the threads doing population
 * @param mti The Masstree index
 * @param ti The Masstree threadinfo for this thread
 * @param shuffled_key_indices Indexes of the keys to be inserted, in order
 * @param num_cores Number of threads doing population
 */
void masstree_populate_func(size_t thread_id, MtIndex *mti, threadinfo_t *ti,
                            const std::vector<size_t> *shuffled_key_indices,
                            size_t num_cores) {
  const size_t num_keys_to_insert_this_thread = FLAGS_num_keys / num_cores;
  size_t num_keys_inserted_this_thread = 0;

  for (size_t i = 0; i < FLAGS_num_keys; i++) {
    if (i % num_cores != thread_id) continue;  // Not this thread's job

    const uint32_t key_index = shuffled_key_indices->at(i);
    uint8_t key[MtIndex::kKeySize];
    uint8_t value[MtIndex::kValueSize];
    key_gen(key_index, key);

    auto *value_32 = reinterpret_cast<uint32_t *>(value);
    for (size_t j = 0; j < MtIndex::kValueSize / sizeof(uint32_t); j++) {
      value_32[j] = get_value32_for_seed(key_index);
    }

    if (kAppVerbose) {
      fprintf(stderr, "PUT: Key: [");
      const uint64_t *key_64 = reinterpret_cast<uint64_t *>(key);
      for (size_t j = 0; j < MtIndex::kKeySize / sizeof(uint64_t); j++) {
        fprintf(stderr, "%zu ", key_64[j]);
      }
      fprintf(stderr, "] Value: [");
      for (size_t j = 0; j < MtIndex::kValueSize; j++) {
        fprintf(stderr, "%u ", value[j]);
      }
      fprintf(stderr, "]\n");
    }

    mti->put(key, value, ti);
    num_keys_inserted_this_thread++;

    // Progress bar
    {
      if (thread_id == 0) {
        const size_t by_20 = num_keys_to_insert_this_thread / 20;
        if (by_20 > 0 && num_keys_inserted_this_thread % by_20 == 0) {
          const double progress_percent = 100.0 *
                                          num_keys_inserted_this_thread /
                                          num_keys_to_insert_this_thread;
          printf("Percent done = %.1f\n", progress_percent);
        }
      }
    }

    if (ctrl_c_pressed == 1) break;
  }
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

    // Create a thread_info for every core, we'll use only some of them for
    // the actual benchmark
    const size_t num_cores = static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
    auto ti_arr = new threadinfo_t *[num_cores];
    for (size_t i = 0; i < num_cores; i++) {
      ti_arr[i] = threadinfo::make(threadinfo::TI_PROCESS, i);
    }

    // Populate the tree in parallel to reduce initialization time
    {
      printf("main: Populating masstree with %zu keys from %zu cores\n",
             FLAGS_num_keys, FLAGS_num_population_threads);

      printf("main: Shuffling key insertion order\n");
      std::vector<size_t> shuffled_key_indices;
      shuffled_key_indices.reserve(FLAGS_num_keys);

      // Populate and shuffle the order in which keys will be inserted
      {
        for (size_t i = 0; i < FLAGS_num_keys; i++) {
          shuffled_key_indices.push_back(i);
        }
        auto rng = std::default_random_engine{};
        std::shuffle(std::begin(shuffled_key_indices),
                     std::end(shuffled_key_indices), rng);
      }

      printf("main: Launching threads to populate Masstree\n");
      std::vector<std::thread> populate_thread_arr(
          FLAGS_num_population_threads);
      for (size_t i = 0; i < FLAGS_num_population_threads; i++) {
        populate_thread_arr[i] =
            std::thread(masstree_populate_func, i, &mti, ti_arr[i],
                        &shuffled_key_indices, FLAGS_num_population_threads);
      }
      for (size_t i = 0; i < FLAGS_num_population_threads; i++)
        populate_thread_arr[i].join();
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
