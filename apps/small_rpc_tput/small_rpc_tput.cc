#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/latency.h"
#include "util/misc.h"

static constexpr bool kAppVerbose = false;  // Print debug info on datapath
static constexpr bool kAppMeasureLatency = false;
static constexpr bool kAppPayloadCheck = false;  // Check full request/response

// Optimization knobs. Set to true to disable optimization.
static constexpr bool kAppOptDisablePreallocResp = false;
static constexpr bool kAppOptDisableRxRingReq = false;

static constexpr size_t kAppReqType = 1;    // eRPC request type
static constexpr uint8_t kAppDataByte = 3;  // Data transferred in req & resp
static constexpr size_t kAppMaxBatchSize = 32;
static constexpr size_t kAppMaxConcurrency = 128;

DEFINE_uint64(batch_size, 0, "Request batch size");
DEFINE_uint64(msg_size, 0, "Request and response size");
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

union tag_t {
  struct {
    uint64_t batch_i : 32;
    uint64_t msgbuf_i : 32;
  } s;

  size_t _tag;

  tag_t(uint64_t batch_i, uint64_t msgbuf_i) {
    s.batch_i = batch_i;
    s.msgbuf_i = msgbuf_i;
  }
  tag_t(size_t _tag) : _tag(_tag) {}
};

static_assert(sizeof(tag_t) == sizeof(size_t), "");

// Per-batch context
class BatchContext {
 public:
  size_t num_resps_rcvd = 0;         // Number of responses received
  size_t req_tsc[kAppMaxBatchSize];  // Timestamp when request was issued
  erpc::MsgBuffer req_msgbuf[kAppMaxBatchSize];
  erpc::MsgBuffer resp_msgbuf[kAppMaxBatchSize];
};

struct app_stats_t {
  double tput_mrps;
  size_t pad[7];
};
static_assert(sizeof(app_stats_t) == 64, "");

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  struct timespec tput_t0;  // Start time for throughput measurement
  app_stats_t *app_stats;

  size_t stat_resp_rx[kAppMaxConcurrency] = {0};  // Resps received for batch i
  size_t stat_resp_rx_tot = 0;  // Total responses received (all batches)
  size_t stat_req_rx_tot = 0;   // Total requests received (all batches)

  std::array<BatchContext, kAppMaxConcurrency> batch_arr;  // Per-batch context
  erpc::Latency latency;  // Cold if latency measurement disabled

  ~AppContext() {}
};

int get_rand_session_num(AppContext *c) {
  // Lemire's trick
  uint32_t x = c->fastrand.next_u32();
  size_t rand_index =
      (static_cast<size_t>(x) * c->session_num_vec.size()) >> 32;
  return c->session_num_vec[rand_index];
}

void app_cont_func(erpc::RespHandle *, void *, size_t);  // Forward declaration

// Try to send requests for batch_i. If requests are successfully sent,
// increment the batch's num_reqs_sent.
void send_reqs(AppContext *c, size_t batch_i) {
  assert(batch_i < FLAGS_concurrency);
  BatchContext &bc = c->batch_arr[batch_i];

  for (size_t i = 0; i < FLAGS_batch_size; i++) {
    if (kAppVerbose) {
      printf("Process %zu, Rpc %u: Sending request for batch %zu.\n",
             FLAGS_process_id, c->rpc->get_rpc_id(), batch_i);
    }

    if (!kAppPayloadCheck) {
      bc.req_msgbuf[i].buf[0] = kAppDataByte;  // Touch req MsgBuffer
    } else {
      // Fill the request MsgBuffer with a checkable sequence
      uint8_t *buf = bc.req_msgbuf[i].buf;
      buf[0] = c->fastrand.next_u32();
      for (size_t j = 1; j < FLAGS_msg_size; j++) buf[j] = buf[0] + j;
    }

    if (kAppMeasureLatency) bc.req_tsc[i] = erpc::rdtsc();

    tag_t tag(batch_i, i);
    c->rpc->enqueue_request(get_rand_session_num(c), kAppReqType,
                            &bc.req_msgbuf[i], &bc.resp_msgbuf[i],
                            app_cont_func, tag._tag);
  }
}

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  c->stat_req_rx_tot++;

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == FLAGS_msg_size);

  // RX ring request optimization knob
  if (kAppOptDisableRxRingReq) {
    // Simulate copying the request off the RX ring
    auto copy_msgbuf = c->rpc->alloc_msg_buffer(FLAGS_msg_size);
    assert(copy_msgbuf.buf != nullptr);
    memcpy(copy_msgbuf.buf, req_msgbuf->buf, FLAGS_msg_size);
    c->rpc->free_msg_buffer(copy_msgbuf);
  }

  // Preallocated response optimization knob
  if (kAppOptDisablePreallocResp) {
    req_handle->prealloc_used = false;
    erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf;
    resp_msgbuf = c->rpc->alloc_msg_buffer(FLAGS_msg_size);
    assert(resp_msgbuf.buf != nullptr);

    if (!kAppPayloadCheck) {
      resp_msgbuf.buf[0] = req_msgbuf->buf[0];
    } else {
      memcpy(resp_msgbuf.buf, req_msgbuf->buf, FLAGS_msg_size);
    }
  } else {
    req_handle->prealloc_used = true;
    erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                   FLAGS_msg_size);

    if (!kAppPayloadCheck) {
      req_handle->pre_resp_msgbuf.buf[0] = req_msgbuf->buf[0];
    } else {
      memcpy(req_handle->pre_resp_msgbuf.buf, req_msgbuf->buf, FLAGS_msg_size);
    }
  }

  c->rpc->enqueue_response(req_handle);
}

void app_cont_func(erpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  const erpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf->get_data_size() == FLAGS_msg_size);

  auto *c = static_cast<AppContext *>(_context);

  if (!kAppPayloadCheck) {
    // Do a cheap check, but touch the response MsgBuffer
    if (unlikely(resp_msgbuf->buf[0] != kAppDataByte)) {
      fprintf(stderr, "Invalid response.\n");
      exit(-1);
    }
  } else {
    // Check the full response MsgBuffer
    for (size_t i = 0; i < FLAGS_msg_size; i++) {
      const uint8_t *buf = resp_msgbuf->buf;
      if (unlikely(buf[i] != static_cast<uint8_t>(buf[0] + i))) {
        fprintf(stderr, "Invalid response byte at index %zu (%u, %u)\n", i,
                buf[0], buf[i]);
        exit(-1);
      }
    }
  }

  c->rpc->release_response(resp_handle);

  auto tag = static_cast<tag_t>(_tag);
  if (kAppVerbose) {
    printf("Received response for batch %zu, msgbuf %zu.\n", tag.s.batch_i,
           tag.s.msgbuf_i);
  }

  BatchContext &bc = c->batch_arr[tag.s.batch_i];
  bc.num_resps_rcvd++;

  if (kAppMeasureLatency) {
    size_t req_tsc = bc.req_tsc[tag.s.msgbuf_i];
    double req_lat_us =
        erpc::to_usec(erpc::rdtsc() - req_tsc, c->rpc->get_freq_ghz());
    c->latency.update(static_cast<size_t>(req_lat_us * 10));
  }

  if (bc.num_resps_rcvd == FLAGS_batch_size) {
    // If we have a full batch, reset batch progress and send more requests
    bc.num_resps_rcvd = 0;
    send_reqs(c, tag.s.batch_i);
  }

  c->stat_resp_rx_tot++;
  c->stat_resp_rx[tag.s.batch_i]++;

  if (unlikely(c->stat_resp_rx_tot == 1000000)) {
    size_t thread_id = c->rpc->get_rpc_id();
    double seconds = erpc::sec_since(c->tput_t0);

    // Min/max responses for a concurrent batch, to check for stagnated batches
    size_t max_resps = 0, min_resps = SIZE_MAX;
    for (size_t i = 0; i < FLAGS_concurrency; i++) {
      min_resps = std::min(min_resps, c->stat_resp_rx[i]);
      max_resps = std::max(max_resps, c->stat_resp_rx[i]);
      c->stat_resp_rx[i] = 0;
    }

    // Session throughput percentiles, used if congestion control is enabled
    std::vector<double> session_tput;
    if (erpc::kCC) {
      for (int session_num : c->session_num_vec) {
        session_tput.push_back(c->rpc->get_session_rate_gbps(session_num));
      }
      std::sort(session_tput.begin(), session_tput.end());
    }

    double tput_mrps = c->stat_resp_rx_tot / (seconds * 1000000);
    c->app_stats[thread_id].tput_mrps = tput_mrps;

    size_t num_sessions = c->session_num_vec.size();
    printf(
        "Process %zu, thread %zu: %.2f Mrps. Average TX batch = %.2f. "
        "Resps RX = %zu, requests RX = %zu. "
        "Resps/concurrent batch: min %zu, max %zu. "
        "Latency: {%.2f, %.2f} us, tput = {%.2f, %.2f, %.2f, %.2f} Gbps.\n",
        FLAGS_process_id, c->thread_id, tput_mrps,
        c->rpc->get_avg_tx_burst_size(), c->stat_resp_rx_tot,
        c->stat_req_rx_tot, min_resps, max_resps,
        kAppMeasureLatency ? c->latency.perc(.50) / 10.0 : -1,
        kAppMeasureLatency ? c->latency.perc(.99) / 10.0 : -1,
        erpc::kCC ? session_tput.at(num_sessions * 0.00) : -1,
        erpc::kCC ? session_tput.at(num_sessions * 0.05) : -1,
        erpc::kCC ? session_tput.at(num_sessions * 0.50) : -1,
        erpc::kCC ? session_tput.at(num_sessions * 0.95) : -1);

    // Thread 1 records stats: throughput
    if (thread_id == 0) {
      double tot_tput_mrps = 0;
      for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
        tot_tput_mrps += c->app_stats[t_i].tput_mrps;
      }
      c->tmp_stat->write(std::to_string(tot_tput_mrps));
    }

    c->rpc->reset_dpath_stats_st();
    c->stat_resp_rx_tot = 0;
    c->stat_req_rx_tot = 0;
    c->latency.reset();

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }
}

// The function executed by each thread in the cluster
void thread_func(size_t thread_id, app_stats_t *app_stats, erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id = thread_id;
  c.app_stats = app_stats;

  if (thread_id == 0) c.tmp_stat = new TmpStat("Mrps");

  uint8_t phy_port = (FLAGS_numa_node == 0) ? numa_0_ports[thread_id % 2]
                                            : numa_1_ports[thread_id % 2];
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);

  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  // Pre-allocate request and response MsgBuffers for each batch
  for (size_t i = 0; i < FLAGS_concurrency; i++) {
    BatchContext &bc = c.batch_arr[i];
    for (size_t j = 0; j < FLAGS_batch_size; j++) {
      bc.req_msgbuf[j] = rpc.alloc_msg_buffer(FLAGS_msg_size);
      assert(bc.req_msgbuf[j].buf != nullptr);

      bc.resp_msgbuf[j] = rpc.alloc_msg_buffer(FLAGS_msg_size);
      assert(bc.resp_msgbuf[j].buf != nullptr);
    }
  }

  // Create a session to each thread in the cluster except self
  for (size_t p_i = 0; p_i < FLAGS_num_processes; p_i++) {
    std::string remote_uri = erpc::get_uri_for_process(p_i);

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      if (FLAGS_process_id == p_i && thread_id == t_i) continue;
      if (FLAGS_sm_verbose == 1) {
        fprintf(stderr, "Process %zu, thread %zu: Creating session to %s.\n",
                FLAGS_process_id, thread_id, remote_uri.c_str());
      }

      int session_num = rpc.create_session(remote_uri, t_i);
      assert(session_num >= 0);
      c.session_num_vec.push_back(session_num);
    }
  }

  erpc::rt_assert(c.session_num_vec.size() ==
                  FLAGS_num_processes * FLAGS_num_threads - 1);

  while (c.num_sm_resps != c.session_num_vec.size()) {
    rpc.run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  fprintf(stderr,
          "Process %zu, thread %zu: All sessions connected. Running ev loop.\n",
          FLAGS_process_id, thread_id);
  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  for (size_t i = 0; i < FLAGS_concurrency; i++) send_reqs(&c, i);

  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(1000);  // 1 second
    if (ctrl_c_pressed == 1) break;
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  erpc::rt_assert(FLAGS_batch_size <= kAppMaxBatchSize, "Invalid batch size");
  erpc::rt_assert(FLAGS_concurrency <= kAppMaxConcurrency, "Invalid concur.");
  erpc::rt_assert(FLAGS_numa_node <= 1, "Invalid NUMA node");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_process_id, FLAGS_numa_node, 0);
  nexus.register_req_func(
      kAppReqType, erpc::ReqFunc(req_handler, erpc::ReqFuncType::kForeground));

  std::vector<std::thread> threads(FLAGS_num_threads);
  auto *app_stats = new app_stats_t[FLAGS_num_threads];

  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = std::thread(thread_func, i, app_stats, &nexus);
    erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
  }

  for (auto &thread : threads) thread.join();
  delete[] app_stats;
}
