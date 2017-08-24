#include <gflags/gflags.h>
#include <papi.h>
#include <signal.h>
#include <cstring>
#include "../apps_common.h"
#include "rpc.h"
#include "util/latency.h"
#include "util/misc.h"

static constexpr bool kAppVerbose = false;
static constexpr size_t kAppMeasureLatency = false;

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr uint8_t kAppDataByte = 3;  // Data transferred in req & resp
static constexpr size_t kAppMaxBatchSize = 32;
static constexpr size_t kMaxConcurrency = 32;

DEFINE_uint64(batch_size, 0, "Request batch size");
DEFINE_uint64(msg_size, 0, "Request and response size");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");

static bool validate_batch_size(const char *, uint64_t batch_size) {
  return batch_size <= kAppMaxBatchSize;
}

static bool validate_concurrency(const char *, uint64_t concurrency) {
  return concurrency <= kMaxConcurrency;
}

DEFINE_validator(batch_size, &validate_batch_size);
DEFINE_validator(concurrency, &validate_concurrency);

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

union tag_t {
  struct {
    uint64_t batch_i : 32;
    uint64_t msgbuf_i : 32;
  };

  size_t _tag;

  tag_t(uint64_t batch_i, uint64_t msgbuf_i)
      : batch_i(batch_i), msgbuf_i(msgbuf_i) {}
  tag_t(size_t _tag) : _tag(_tag) {}
};

static_assert(sizeof(tag_t) == sizeof(size_t), "");

// Per-batch context
class BatchContext {
 public:
  size_t num_reqs_sent = 0;          // Number of requests sent
  size_t num_resps_rcvd = 0;         // Number of responses received
  size_t req_tsc[kAppMaxBatchSize];  // Timestamp when request was issued
  ERpc::MsgBuffer req_msgbuf[kAppMaxBatchSize];
  ERpc::MsgBuffer resp_msgbuf[kAppMaxBatchSize];
};

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  // The entry in session_num_vec for this thread, to avoid self-request
  size_t self_session_index;
  struct timespec tput_t0;  // Start time for throughput measurement

  size_t stat_resp_rx[kMaxConcurrency] = {0};  // Resps received for batch i
  size_t stat_resp_rx_tot = 0;  // Total responses received (all batches)
  size_t stat_req_rx_tot = 0;   // Total requests received (all batches)

  std::array<BatchContext, kMaxConcurrency> batch_arr;  // Per-batch context
  ERpc::Latency latency;  // Cold if latency measurement disabled

  ~AppContext() {}
};

size_t get_rand_session_index(AppContext *c) {
  assert(c != nullptr);
  const size_t num_sessions = c->session_num_vec.size();

  // Use Lemire's trick to compute random numbers modulo c->num_sessions
  uint32_t x = c->fastrand.next_u32();
  size_t rand_session_index = (static_cast<size_t>(x) * num_sessions) >> 32;
  while (rand_session_index == c->self_session_index) {
    x = c->fastrand.next_u32();
    rand_session_index = (static_cast<size_t>(x) * num_sessions) >> 32;
  }

  return rand_session_index;
}

void app_cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration

// Try to send requests for batch_i. If requests are successfully sent,
// increment the batch's num_reqs_sent.
void send_reqs(AppContext *c, size_t batch_i) {
  assert(c != nullptr);
  assert(batch_i < FLAGS_concurrency);

  BatchContext &bc = c->batch_arr[batch_i];
  assert(bc.num_reqs_sent < FLAGS_batch_size);

  size_t initial_num_reqs_sent = bc.num_reqs_sent;
  for (size_t i = 0; i < FLAGS_batch_size - initial_num_reqs_sent; i++) {
    size_t rand_session_index = get_rand_session_index(c);
    size_t msgbuf_index = initial_num_reqs_sent + i;
    if (kAppVerbose) {
      printf("Sending request for batch %zu, msgbuf_index = %zu.\n", batch_i,
             msgbuf_index);
    }

    bc.req_msgbuf[msgbuf_index].buf[0] = kAppDataByte;  // Touch req MsgBuffer

    if (kAppMeasureLatency) bc.req_tsc[msgbuf_index] = ERpc::rdtsc();
    tag_t tag(batch_i, msgbuf_index);
    int ret = c->rpc->enqueue_request(c->session_num_vec[rand_session_index],
                                      kAppReqType, &bc.req_msgbuf[msgbuf_index],
                                      &bc.resp_msgbuf[msgbuf_index],
                                      app_cont_func, tag._tag);
    assert(ret == 0 || ret == -EBUSY);
    if (ret == -EBUSY) {
      return;
    } else {
      bc.num_reqs_sent++;
    }
  }
}

void req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *c = static_cast<AppContext *>(_context);
  c->stat_req_rx_tot++;

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t resp_size = req_msgbuf->get_data_size();
  ERpc::Rpc<ERpc::IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                  resp_size);
  req_handle->pre_resp_msgbuf.buf[0] = kAppDataByte;  // Touch resp MsgBuffer
  req_handle->prealloc_used = true;

  // c->rpc->nano_sleep(20);
  c->rpc->enqueue_response(req_handle);
}

void app_cont_func(ERpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  const ERpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf != nullptr);

  // Touch (and check) resp MsgBuffer
  if (unlikely(resp_msgbuf->buf[0] != kAppDataByte)) {
    fprintf(stderr, "Invalid response.\n");
    exit(-1);
  }

  tag_t tag = static_cast<tag_t>(_tag);
  if (kAppVerbose) {
    printf("Received response for batch %zu, msgbuf %zu.\n", tag.batch_i,
           tag.msgbuf_i);
  }

  auto *c = static_cast<AppContext *>(_context);
  BatchContext &bc = c->batch_arr[tag.batch_i];
  bc.num_resps_rcvd++;

  if (kAppMeasureLatency) {
    size_t req_tsc = bc.req_tsc[tag.msgbuf_i];
    double req_lat_us =
        ERpc::to_usec(ERpc::rdtsc() - req_tsc, c->rpc->get_freq_ghz());
    c->latency.update(static_cast<size_t>(req_lat_us * 10));
  }

  if (bc.num_resps_rcvd == FLAGS_batch_size) {
    // If we have a full batch, reset batch progress and send more requests
    assert(bc.num_reqs_sent == FLAGS_batch_size);
    bc.num_reqs_sent = 0;
    bc.num_resps_rcvd = 0;
    send_reqs(c, tag.batch_i);
  } else if (bc.num_reqs_sent != FLAGS_batch_size) {
    // If we haven't sent a full batch, send more requests
    send_reqs(c, tag.batch_i);
  }

  // Try to send more requests for stagnated batches. This happens when we
  // are unable to send requests for a batch because a session is clogged.
  for (size_t batch_j = 0; batch_j < FLAGS_concurrency; batch_j++) {
    if (batch_j == tag.batch_i) continue;

    if (c->batch_arr[batch_j].num_resps_rcvd ==
        c->batch_arr[batch_j].num_reqs_sent) {
      assert(c->batch_arr[batch_j].num_reqs_sent != FLAGS_batch_size);
      send_reqs(c, batch_j);
    }
  }

  c->stat_resp_rx_tot++;
  c->stat_resp_rx[tag.batch_i]++;

  if (c->stat_resp_rx_tot == 1000000) {
    double seconds = ERpc::sec_since(c->tput_t0);

    // Min/max responses for a concurrent batch, to check for stagnated batches.
    size_t max_resps = 0, min_resps = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < FLAGS_concurrency; i++) {
      min_resps = std::min(min_resps, c->stat_resp_rx[i]);
      max_resps = std::max(max_resps, c->stat_resp_rx[i]);
    }

    float ipc = -1.0;
    if (FLAGS_num_threads == 1) {
      float real_time, proc_time;
      long long ins;
      int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
      if (ret < PAPI_OK) {
        fprintf(stderr, "PAPI IPC failed.\n");
        exit(-1);
      }
    }

    printf(
        "Thread %zu: Throughput = %.2f Mrps. Average TX batch size = %.2f. "
        "Resps RX = %zu, requests RX = %zu. "
        "Resps/concurrent batch: min %zu, max %zu. IPC = %.2f. "
        "Latency: %.2f us avg, %.2f us 99 perc.\n",
        c->thread_id, c->stat_resp_rx_tot / (seconds * 1000000),
        c->rpc->get_avg_tx_burst_size(), c->stat_resp_rx_tot,
        c->stat_req_rx_tot, min_resps, max_resps, ipc, c->latency.avg() / 10.0,
        c->latency.perc(.99) / 10.0);

    // Stats: throughput ipc
    c->tmp_stat->write(
        std::to_string(c->stat_resp_rx_tot / (seconds * 1000000)) + " " +
        std::to_string(ipc));

    c->rpc->reset_dpath_stats_st();
    c->stat_resp_rx_tot = 0;
    c->stat_req_rx_tot = 0;
    c->latency.reset();

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  c->rpc->release_response(resp_handle);
}

// The function executed by each thread in the cluster
void thread_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus) {
  AppContext c;
  c.tmp_stat = new TmpStat("small_rpc_tput", "Mrps IPC");
  c.thread_id = thread_id;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&c),
                                   static_cast<uint8_t>(thread_id),
                                   basic_sm_handler, kAppPhyPort, kAppNumaNode);
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

  c.session_num_vec.resize(FLAGS_num_machines * FLAGS_num_threads);
  c.self_session_index = FLAGS_machine_id * FLAGS_num_threads + thread_id;

  // Initiate connection for sessions
  for (size_t m_i = 0; m_i < FLAGS_num_machines; m_i++) {
    std::string hostname = get_hostname_for_machine(m_i);

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      const size_t session_idx = (m_i * FLAGS_num_threads) + t_i;
      if (session_idx == c.self_session_index) continue;  // No session to self

      c.session_num_vec[session_idx] =
          rpc.create_session(hostname, static_cast<uint8_t>(t_i), kAppPhyPort);
      assert(c.session_num_vec[session_idx] >= 0);
    }
  }

  while (c.num_sm_resps != FLAGS_num_machines * FLAGS_num_threads - 1) {
    rpc.run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  fprintf(stderr, "Thread %zu: All sessions connected. Running event loop.\n",
          thread_id);
  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  // Initialize PAPI measurement if we're running one thread
  if (FLAGS_num_threads == 1) {
    float real_time, proc_time, ipc;
    long long ins;
    int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
    if (ret < PAPI_OK) {
      fprintf(stderr, "PAPI initialization failed.\n");
      exit(-1);
    }
  }

  for (size_t i = 0; i < FLAGS_concurrency; i++) send_reqs(&c, i);

  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(1000);  // 1 second
    if (ctrl_c_pressed == 1) break;
  }
}

int main(int argc, char **argv) {
  assert(FLAGS_num_bg_threads == 0);  // XXX: Need to change ReqFuncType below
  signal(SIGINT, ctrl_c_handler);

  // Work around g++-5's unused variable warning for validators
  _unused(batch_size_validator_registered);
  _unused(concurrency_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name.c_str(), kAppNexusUdpPort,
                                       FLAGS_num_bg_threads);
  nexus.register_req_func(
      kAppReqType, ERpc::ReqFunc(req_handler, ERpc::ReqFuncType::kForeground));

  std::thread threads[FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = std::thread(thread_func, i, &nexus);
    ERpc::bind_to_core(threads[i], i);
  }

  for (size_t i = 0; i < FLAGS_num_threads; i++) threads[i].join();
}
