#include "log.h"
#include <gflags/gflags.h>
#include <libpmem.h>
#include <cstring>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/numautils.h"
#include "util/pmem.h"
#include "util/virt2phy.h"

DEFINE_uint64(num_proc_0_threads, 0, "Threads in process with ID = 0");
DEFINE_uint64(num_proc_other_threads, 0, "Threads in process with ID != 0");
DEFINE_uint64(concurrency, 0, "Concurrent requests per client thread");
DEFINE_uint64(min_log_entry_size, 0, "Min size of log entries sent by client");
DEFINE_uint64(max_log_entry_size, 0, "Size of log entries sent by client");
DEFINE_uint64(prints_per_log_entry_size, 0, "Prints before switching size");
DEFINE_uint64(use_rotating, 0, "Use rotating counter for log");

static constexpr size_t kAppReqType = 1;
static constexpr size_t kAppEvLoopMs = 1000;  // Duration of event loop
static constexpr bool kAppVerbose = false;
static constexpr const char *kAppPmemFile = "/dev/dax0.0";
static constexpr size_t kAppMaxConcurrency = 32;  // Outstanding reqs per thread
static constexpr size_t kAppPmemFileSize = GB(4);

class ServerContext : public BasicAppContext {
 public:
  Log log;

  // The server prints rate statistics along with a log entry size. Although
  // the clients switch their log entry size nearly in sync, a server
  // measurement epoch may receive log entries of different sizes. The fields
  // below mark those epochs in stdout in which all log entries were of the same
  // size.
  size_t first_log_entry_size;  // Size of first log entry in measurement epoch
  bool all_first_log_entry_size;  // True iff all sizeof log entries = first

  struct timespec tput_t0;  // Start time for throughput measurement
  size_t num_reqs_completed;
};

// Per-thread application context
class ClientContext : public BasicAppContext {
 public:
  struct timespec tput_t0;  // Start time for throughput measurement
  size_t num_reqs_completed = 0;
  size_t cur_log_entry_size = 0;

  erpc::MsgBuffer req_msgbuf[kAppMaxConcurrency];
  erpc::MsgBuffer resp_msgbuf[kAppMaxConcurrency];
};

void app_cont_func(void *, void *);  // Forward declaration

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<ServerContext *>(_context);

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  _unused(req_msgbuf);
  size_t log_entry_size = req_msgbuf->get_data_size();

  if (unlikely(c->first_log_entry_size == SIZE_MAX)) {
    c->first_log_entry_size = log_entry_size;
  }

  if (unlikely(log_entry_size != c->first_log_entry_size)) {
    c->all_first_log_entry_size = false;
  }

  if (FLAGS_use_rotating == 1) {
    c->log.append_rotating(req_msgbuf->buf, req_msgbuf->get_data_size());
  } else {
    c->log.append_naive(req_msgbuf->buf, req_msgbuf->get_data_size());
  }

  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                 sizeof(size_t));
  c->rpc->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf);
  c->num_reqs_completed++;
}

// The function executed by each client thread in the cluster
void server_func(size_t thread_id, erpc::Nexus *nexus, uint8_t *pbuf) {
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ServerContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), thread_id,
                                  basic_sm_handler, phy_port);

  c.rpc = &rpc;
  c.thread_id = thread_id;
  c.log = Log(pbuf, kAppPmemFileSize);

  while (true) {
    clock_gettime(CLOCK_REALTIME, &c.tput_t0);
    c.first_log_entry_size = SIZE_MAX;
    c.all_first_log_entry_size = true;

    rpc.run_event_loop(kAppEvLoopMs);

    double sec = erpc::sec_since(c.tput_t0);
    printf(
        "log server (rotating: %s): Thread %zu, "
        "[first_log_entry_size %zu, all_first_log_entry_size = %s], "
        "rate %.2f M/s\n",
        FLAGS_use_rotating == 1 ? "yes" : "no", c.thread_id,
        c.first_log_entry_size, c.all_first_log_entry_size ? "yes" : "no",
        c.num_reqs_completed / (1000000.0 * sec));

    clock_gettime(CLOCK_REALTIME, &c.tput_t0);
    c.num_reqs_completed = 0;
  }
}

// Send a request using this MsgBuffer
void send_req(ClientContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_msgbuf,
                                                 c->cur_log_entry_size);

  if (kAppVerbose) {
    printf("log: Thread %zu sending request using msgbuf_idx %zu.\n",
           c->thread_id, msgbuf_idx);
  }

  c->rpc->enqueue_request(c->session_num_vec[0], kAppReqType, &req_msgbuf,
                          &c->resp_msgbuf[msgbuf_idx], app_cont_func,
                          reinterpret_cast<void *>(msgbuf_idx));
}

void app_cont_func(void *_context, void *_tag) {
  auto *c = static_cast<ClientContext *>(_context);
  auto msgbuf_idx = reinterpret_cast<size_t>(_tag);

  if (kAppVerbose) printf("log: Received resp for msgbuf %zu.\n", msgbuf_idx);

  c->num_reqs_completed++;
  send_req(c, msgbuf_idx);
}

void client_connect_sessions(BasicAppContext *c) {
  // All non-zero processes create one session to process #0
  if (FLAGS_process_id == 0) return;

  size_t global_thread_id =
      FLAGS_process_id * FLAGS_num_proc_other_threads + c->thread_id;
  size_t rem_tid = global_thread_id % FLAGS_num_proc_0_threads;

  c->session_num_vec.resize(1);

  printf("log: Thread %zu: Creating session to proc 0, thread %zu.\n",
         c->thread_id, rem_tid);

  c->session_num_vec[0] =
      c->rpc->create_session(erpc::get_uri_for_process(0), rem_tid);
  erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session() failed");

  while (c->num_sm_resps != 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
  }
}

// The function executed by each client thread in the cluster
void client_func(size_t thread_id, erpc::Nexus *nexus) {
  ClientContext c;
  c.thread_id = thread_id;

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;
  c.cur_log_entry_size = FLAGS_min_log_entry_size;

  client_connect_sessions(&c);

  if (c.session_num_vec.size() > 0) {
    printf("log: Thread %zu: All sessions connected.\n", thread_id);
  } else {
    printf("log: Thread %zu: No sessions created.\n", thread_id);
  }

  for (size_t i = 0; i < FLAGS_concurrency; i++) {
    c.req_msgbuf[i] = c.rpc->alloc_msg_buffer_or_die(FLAGS_max_log_entry_size);
    memset(c.req_msgbuf[i].buf, i + 1, FLAGS_max_log_entry_size);

    c.resp_msgbuf[i] = c.rpc->alloc_msg_buffer_or_die(sizeof(size_t));
  }

  for (size_t msgbuf_idx = 0; msgbuf_idx < FLAGS_concurrency; msgbuf_idx++) {
    send_req(&c, msgbuf_idx);
  }

  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  size_t num_prints = 0;
  for (size_t i = 0; i < FLAGS_test_ms; i += kAppEvLoopMs) {
    rpc.run_event_loop(kAppEvLoopMs);
    if (c.session_num_vec.size() == 0) continue;  // No stats to print

    double sec = erpc::sec_since(c.tput_t0);
    printf(
        "Thread %zu, log entry size %zu: rate %.2f M/s. "
        "Credits %zu (best = 32).\n",
        c.thread_id, c.cur_log_entry_size,
        c.num_reqs_completed / (1000000.0 * sec), erpc::kSessionCredits);

    clock_gettime(CLOCK_REALTIME, &c.tput_t0);
    c.num_reqs_completed = 0;

    num_prints++;
    if (num_prints == FLAGS_prints_per_log_entry_size) {
      c.cur_log_entry_size *= 2;
      if (c.cur_log_entry_size > FLAGS_max_log_entry_size) {
        c.cur_log_entry_size = FLAGS_min_log_entry_size;
      }
      num_prints = 0;
    }
  }

  // We don't disconnect sessions
}

int main(int argc, char **argv) {
  static_assert(erpc::kZeroCopyRX == true, "Enable zero-copy RX for perf");

  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_concurrency <= kAppMaxConcurrency, "Invalid conc");
  erpc::rt_assert(FLAGS_process_id < FLAGS_num_processes, "Invalid process ID");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  size_t num_threads = FLAGS_process_id == 0 ? 1 : FLAGS_num_proc_other_threads;
  std::vector<std::thread> threads(num_threads);

  if (FLAGS_process_id == 0) {
    printf("Server: Mapping pmem file");
    uint8_t *pbuf = erpc::map_devdax_file(kAppPmemFile, kAppPmemFileSize);
    printf("Server: Done.\n");

    for (size_t i = 0; i < num_threads; i++) {
      threads[i] = std::thread(server_func, i, &nexus, pbuf);
      erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
    }
  } else {
    for (size_t i = 0; i < num_threads; i++) {
      threads[i] = std::thread(client_func, i, &nexus);
      erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
    }
  }

  for (auto &thread : threads) thread.join();
}
