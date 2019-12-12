/** @file pmem_bw.cc
 *
 * @brief Benchmark to measure throughput of large writes to remote NVM. Each
 * client thread creates one session to the server thread.
 */

#include "pmem_bw.h"
#include <libpmem.h>
#include <rte_rawdev.h>
// Newline to prevent reordering
#include <rte_ioat_rawdev.h>
#include <signal.h>
#include <cstring>
#include "util/autorun_helpers.h"
#include "util/pmem.h"

static constexpr size_t kAppEvLoopMs = 1000;  // Duration of event loop
static constexpr bool kAppVerbose = false;
static constexpr const char *kAppPmemFile = "/dev/dax0.0";
static constexpr size_t kAppPmemFileSize = GB(32);

// IOAT
static constexpr bool kUseIoat = true;
static constexpr size_t kIoatDevID = 0;
static constexpr size_t kIoatRingSize = 512;

void app_cont_func(void *, void *);  // Forward declaration

// Send a request using this MsgBuffer
void send_req(ClientContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == FLAGS_req_size);

  if (kAppVerbose) {
    printf("large_rpc_tput: Thread %zu sending request using msgbuf_idx %zu.\n",
           c->thread_id, msgbuf_idx);
  }

  c->rpc->enqueue_request(c->session_num_vec[0], kAppReqType, &req_msgbuf,
                          &c->resp_msgbuf[msgbuf_idx], app_cont_func,
                          reinterpret_cast<void *>(msgbuf_idx));

  c->stat_tx_bytes_tot += FLAGS_req_size;
}

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<ServerContext *>(_context);

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  erpc::rt_assert(req_msgbuf->get_data_size() == FLAGS_req_size);

  if (c->pmem.cur_offset + FLAGS_req_size >= c->pmem.offset_hi) {
    c->pmem.cur_offset = c->pmem.offset_lo;
  }

  size_t start = erpc::rdtsc();

  if (kUseIoat) {
    // The pmem file has contiguous physical addresses
    uint64_t dst_paddr =
        c->pmem.offset_lo_paddr + (c->pmem.cur_offset - c->pmem.offset_lo);
    uint64_t src_paddr = c->hpcaching_v2p.translate(req_msgbuf->buf);

    int ret = rte_ioat_enqueue_copy(kIoatDevID, src_paddr, dst_paddr,
                                    FLAGS_req_size, 0, 0, 0);
    erpc::rt_assert(ret == 1, "Error with rte_ioat_enqueue_copy");

    rte_ioat_do_copies(kIoatDevID);

    while (true) {
      uintptr_t _src, _dst;
      int ret = rte_ioat_completed_copies(kIoatDevID, 1u, &_src, &_dst);
      erpc::rt_assert(ret >= 0, "rte_ioat_completed_copies error");

      if (ret > 0) break;
    }
  } else {
    pmem_memcpy_persist(&c->pbuf[c->pmem.cur_offset], req_msgbuf->buf,
                        FLAGS_req_size);
  }

  c->pmem.write_bytes += FLAGS_req_size;
  c->pmem.write_cycles += (erpc::rdtsc() - start);

  if (c->pmem.write_bytes >= GB(2)) {
    size_t wr_nsec =
        erpc::to_nsec(c->pmem.write_cycles, c->rpc->get_freq_ghz());
    printf("Server thread %zu: Pmem write tput = %.2f GB/s\n", c->thread_id,
           c->pmem.write_bytes * 1.0 / wr_nsec);

    c->pmem.write_bytes = 0;
    c->pmem.write_cycles = 0;
  }

  c->pmem.cur_offset += FLAGS_req_size;
  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                 FLAGS_resp_size);
  c->rpc->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf);
}

// Initialize and start device kIoatDevID
void setup_ioat_device() {
  // RTE_LOG_WARNING is log level 5
  const char *rte_argv[] = {"-c", "1",  "-n",  "6", "--log-level",
                            "5",  "-m", "128", NULL};

  int rte_argc = sizeof(rte_argv) / sizeof(rte_argv[0]) - 1;
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  erpc::rt_assert(ret >= 0, "rte_eal_init failed");

  struct rte_rawdev_info info;
  info.dev_private = nullptr;

  erpc::rt_assert(rte_rawdev_info_get(kIoatDevID, &info) == 0);
  erpc::rt_assert(std::string(info.driver_name).find("ioat") !=
                  std::string::npos);

  struct rte_ioat_rawdev_config p;
  memset(&info, 0, sizeof(info));
  info.dev_private = &p;

  rte_rawdev_info_get(kIoatDevID, &info);
  erpc::rt_assert(p.ring_size == 0, "Initial ring size is non-zero");

  p.ring_size = kIoatRingSize;
  erpc::rt_assert(rte_rawdev_configure(kIoatDevID, &info) == 0,
                  "rte_rawdev_configure failed");

  rte_rawdev_info_get(kIoatDevID, &info);
  erpc::rt_assert(p.ring_size == kIoatRingSize, "Wrong ring size");

  erpc::rt_assert(rte_rawdev_start(kIoatDevID) == 0, "Rawdev start failed");

  printf("Started IOAT device %zu\n", kIoatDevID);
}

// The function executed by each client thread in the cluster
void server_func(size_t thread_id, erpc::Nexus *nexus, uint8_t *pbuf) {
  setup_ioat_device();
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ServerContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), thread_id,
                                  basic_sm_handler, phy_port);
  erpc::rt_assert(FLAGS_resp_size <= rpc.get_max_data_per_pkt());

  c.rpc = &rpc;
  c.thread_id = thread_id;
  c.pbuf = pbuf;
  c.pmem.offset_lo = (kAppPmemFileSize / FLAGS_num_proc_0_threads) * thread_id;
  c.pmem.offset_hi =
      (kAppPmemFileSize / FLAGS_num_proc_0_threads) * (thread_id + 1);
  c.pmem.cur_offset = c.pmem.offset_lo;

  if (kUseIoat) {
    c.pmem.offset_lo_paddr = c.hpcaching_v2p.translate(&pbuf[c.pmem.offset_lo]);
  }

  while (true) {
    rpc.run_event_loop(1000);
    if (ctrl_c_pressed == 1) break;
  }
}

void app_cont_func(void *_context, void *_tag) {
  auto *c = static_cast<ClientContext *>(_context);
  auto msgbuf_idx = reinterpret_cast<size_t>(_tag);

  const erpc::MsgBuffer &resp_msgbuf = c->resp_msgbuf[msgbuf_idx];
  if (kAppVerbose) {
    printf("large_rpc_tput: Received response for msgbuf %zu.\n", msgbuf_idx);
  }

  erpc::rt_assert(resp_msgbuf.get_data_size() == FLAGS_resp_size,
                  "Invalid response size");
  c->stat_rx_bytes_tot += FLAGS_resp_size;
  send_req(c, msgbuf_idx);
}

void client_connect_sessions(BasicAppContext *c) {
  // All non-zero processes create one session to process #0
  if (FLAGS_process_id == 0) return;

  size_t global_thread_id =
      FLAGS_process_id * FLAGS_num_proc_other_threads + c->thread_id;
  size_t rem_tid = global_thread_id % FLAGS_num_proc_0_threads;

  c->session_num_vec.resize(1);

  printf(
      "large_rpc_tput: Thread %zu: Creating 1 session to proc 0, thread %zu.\n",
      c->thread_id, rem_tid);

  c->session_num_vec[0] =
      c->rpc->create_session(erpc::get_uri_for_process(0), rem_tid);
  erpc::rt_assert(c->session_num_vec[0] >= 0, "create_session() failed");

  while (c->num_sm_resps != 1) {
    c->rpc->run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }
}

// The function executed by each client thread in the cluster
void client_func(size_t thread_id, app_stats_t *app_stats, erpc::Nexus *nexus) {
  ClientContext c;
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

  client_connect_sessions(&c);

  if (c.session_num_vec.size() > 0) {
    printf("large_rpc_tput: Thread %zu: All sessions connected.\n", thread_id);
  } else {
    printf("large_rpc_tput: Thread %zu: No sessions created.\n", thread_id);
  }

  alloc_req_resp_msg_buffers(&c);

  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  // Any thread that creates a session sends requests
  if (c.session_num_vec.size() > 0) {
    for (size_t msgbuf_idx = 0; msgbuf_idx < FLAGS_concurrency; msgbuf_idx++) {
      send_req(&c, msgbuf_idx);
    }
  }

  clock_gettime(CLOCK_REALTIME, &c.tput_t0);
  for (size_t i = 0; i < FLAGS_test_ms; i += kAppEvLoopMs) {
    rpc.run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) break;
    if (c.session_num_vec.size() == 0) continue;  // No stats to print

    double ns = erpc::ns_since(c.tput_t0);  // Don't rely on kAppEvLoopMs

    // Publish stats
    auto &stats = c.app_stats[c.thread_id];
    stats.rx_gbps = c.stat_rx_bytes_tot * 8 / ns;
    stats.tx_gbps = c.stat_tx_bytes_tot * 8 / ns;

    // Reset stats for next iteration
    c.stat_rx_bytes_tot = 0;
    c.stat_tx_bytes_tot = 0;
    c.rpc->reset_num_re_tx(c.session_num_vec[0]);

    printf(
        "large_rpc_tput: Thread %zu: Tput {RX %.2f, TX %.2f} Gbps. "
        "Credits %zu (best = 32).\n",
        c.thread_id, stats.rx_gbps, stats.tx_gbps, erpc::kSessionCredits);

    clock_gettime(CLOCK_REALTIME, &c.tput_t0);
  }

  // We don't disconnect sessions
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_concurrency <= kAppMaxConcurrency, "Invalid conc");
  erpc::rt_assert(FLAGS_process_id < FLAGS_num_processes, "Invalid process ID");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  size_t num_threads = FLAGS_process_id == 0 ? FLAGS_num_proc_0_threads
                                             : FLAGS_num_proc_other_threads;
  std::vector<std::thread> threads(num_threads);

  if (FLAGS_process_id == 0) {
    // Each thread needs at least FLAGS_req_size space in the buffer
    erpc::rt_assert(kAppPmemFileSize >= FLAGS_req_size * num_threads);

    printf("Server: Mapping pmem file for all threads...");
    uint8_t *pbuf = erpc::map_devdax_file(kAppPmemFile, kAppPmemFileSize);
    printf("Server: Done.\n");

    for (size_t i = 0; i < num_threads; i++) {
      threads[i] = std::thread(server_func, i, &nexus, pbuf);
      erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
    }
  } else {
    auto *app_stats = new app_stats_t[num_threads];  // Leaked

    for (size_t i = 0; i < num_threads; i++) {
      threads[i] = std::thread(client_func, i, app_stats, &nexus);
      erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
    }
  }

  for (auto &thread : threads) thread.join();
}
