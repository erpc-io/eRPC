/** @file pmem_bw.cc
 *
 * @brief Benchmark to measure throughput of large writes to remote NVM. Each
 * client thread creates one session to the server thread.
 */

#include "pmem_bw.h"
#include <libpmem.h>
#include <rte_rawdev.h>
// Newline to prevent reordering rte_ioat_rawdev.h before rte_rawdev.h
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
static constexpr size_t kIoatDevID = 0;
static constexpr size_t kIoatRingSize = 512;

void app_cont_func(void *, void *);  // Forward declaration

struct dma_context_t {
  size_t file_offset;
  size_t req_size;
};

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<ServerContext *>(_context);

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t req_size = req_msgbuf->get_data_size();

  if (c->pmem.cur_offset + req_size >= c->pmem.offset_hi) {
    c->pmem.cur_offset = c->pmem.offset_lo;
  }

  if (FLAGS_use_ioat == 1) {
    // Save the DMA context into pre_resp_msgbuf to save a dynamic allocation
    auto *dma_ctx =
        reinterpret_cast<dma_context_t *>(req_handle->pre_resp_msgbuf.buf);
    dma_ctx->file_offset = c->pmem.cur_offset;
    dma_ctx->req_size = req_size;

    // The pmem file has contiguous physical addresses
    uint64_t dst_paddr = c->pmem.file_base_paddr + c->pmem.cur_offset;
    erpc::rt_assert(dst_paddr % 64 == 0,
                    "Destination physical address isn't 64-byte aligned, which "
                    "causes poor IOAT DMA performance");

    *reinterpret_cast<size_t *>(req_handle->pre_resp_msgbuf.buf) =
        c->pmem.cur_offset;

    uint64_t src_paddr = c->hpcaching_v2p.translate(req_msgbuf->buf);

    int ret =
        rte_ioat_enqueue_copy(kIoatDevID, src_paddr, dst_paddr, req_size,
                              reinterpret_cast<uintptr_t>(req_handle), 0, 0);
    erpc::rt_assert(ret == 1, "Error with rte_ioat_enqueue_copy");

    rte_ioat_do_copies(kIoatDevID);

  } else {
    pmem_memcpy_persist(&c->pbuf[c->pmem.cur_offset], req_msgbuf->buf,
                        req_size);
    erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                   FLAGS_resp_size);
    c->rpc->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf);
  }

  c->pmem.cur_offset += req_size;
}

// Initialize and start device kIoatDevID
void setup_ioat_device() {
  // RTE_LOG_WARNING is log level 5
  const char *rte_argv[] = {"-c", "1",  "-n",  "6", "--log-level",
                            "5",  "-m", "128", NULL};

  int rte_argc = sizeof(rte_argv) / sizeof(rte_argv[0]) - 1;
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  erpc::rt_assert(ret >= 0, "rte_eal_init failed");

  size_t count = rte_rawdev_count();
  printf("Fount %zu rawdev devices\n", count);
  erpc::rt_assert(count >= 1, "No rawdev devices available");

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

  if (FLAGS_use_ioat == 1) {
    c.pmem.file_base_paddr = c.hpcaching_v2p.translate(&pbuf[0]);

    // Check physical address continuity at some random addresses
    erpc::SlowRand slow_rand;
    for (size_t i = 0; i < 1000; i++) {
      size_t rand_offset = slow_rand.next_u64() % kAppPmemFileSize;

      erpc::rt_assert(c.hpcaching_v2p.translate(&pbuf[rand_offset]) ==
                          c.pmem.file_base_paddr + rand_offset,
                      "Error: Pmem file physical addresses are not contiguous");
    }
  }

  while (true) {
    rpc.run_event_loop_once();

    // Check for IOAT DMA completions
    while (FLAGS_use_ioat == 1 && true) {
      uintptr_t _req_handle_cb, _dummy_cb;
      int ret = rte_ioat_completed_copies(kIoatDevID, 1u, &_req_handle_cb,
                                          &_dummy_cb);

      erpc::rt_assert(ret >= 0, "rte_ioat_completed_copies error");
      if (ret == 0) break;  // No new completions

      // We have a DMA completion
      auto *req_handle = reinterpret_cast<erpc::ReqHandle *>(_req_handle_cb);

      {
        // Check DMA copy result
        auto *dma_ctx =
            reinterpret_cast<dma_context_t *>(req_handle->pre_resp_msgbuf.buf);

        size_t rand_msgbug_index = c.fastrand.next_u32() % dma_ctx->req_size;

        uint8_t pmem_file_byte =
            c.pbuf[dma_ctx->file_offset + rand_msgbug_index];
        uint8_t msgbuf_byte =
            req_handle->get_req_msgbuf()->buf[rand_msgbug_index];

        if (unlikely(pmem_file_byte != msgbuf_byte)) {
          fprintf(stderr, "DMA copy fail: File [%zu, %u], msgbuf [%zu, %u].\n",
                  dma_ctx->file_offset, pmem_file_byte, rand_msgbug_index,
                  msgbuf_byte);
        }
      }

      erpc::Rpc<erpc::CTransport>::resize_msg_buffer(
          &req_handle->pre_resp_msgbuf, FLAGS_resp_size);
      c.rpc->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf);
    }

    if (ctrl_c_pressed == 1) break;
  }
}

// Send a request using this MsgBuffer
void send_req(ClientContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_msgbuf, c->cur_req_size);

  if (kAppVerbose) {
    printf("pmem_bw: Thread %zu sending request using msgbuf_idx %zu.\n",
           c->thread_id, msgbuf_idx);
  }

  c->rpc->enqueue_request(c->session_num_vec[0], kAppReqType, &req_msgbuf,
                          &c->resp_msgbuf[msgbuf_idx], app_cont_func,
                          reinterpret_cast<void *>(msgbuf_idx));

  c->stat_tx_bytes_tot += c->cur_req_size;
}

void app_cont_func(void *_context, void *_tag) {
  auto *c = static_cast<ClientContext *>(_context);
  auto msgbuf_idx = reinterpret_cast<size_t>(_tag);

  const erpc::MsgBuffer &resp_msgbuf = c->resp_msgbuf[msgbuf_idx];
  if (kAppVerbose) {
    printf("pmem_bw: Received response for msgbuf %zu.\n", msgbuf_idx);
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

  printf("pmem_bw: Thread %zu: Creating 1 session to proc 0, thread %zu.\n",
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
  c.cur_req_size = FLAGS_min_req_size;

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
    printf("pmem_bw: Thread %zu: All sessions connected.\n", thread_id);
  } else {
    printf("pmem_bw: Thread %zu: No sessions created.\n", thread_id);
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

  size_t num_prints = 0;
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
        "pmem_bw: Thread %zu, cur_req_size %zu: "
        "Tput {RX %.2f, TX %.2f} Gbps. Credits %zu (best = 32).\n",
        c.thread_id, c.cur_req_size, stats.rx_gbps, stats.tx_gbps,
        erpc::kSessionCredits);

    clock_gettime(CLOCK_REALTIME, &c.tput_t0);
    num_prints++;
    if (num_prints == 2) {
      num_prints = 0;
      c.cur_req_size *= 2;
      if (c.cur_req_size > FLAGS_max_req_size) {
        c.cur_req_size = FLAGS_min_req_size;
      }
    }
  }

  // We don't disconnect sessions
}

int main(int argc, char **argv) {
  static_assert(erpc::kZeroCopyRX == false,
                "This test needs RX ring--independent requests");
  static_assert(erpc::kEnableCc == false,
                "Disable congestion control for performance");
  static_assert(erpc::kSessionReqWindow >= 32,
                "Increase eRPC's session request window for performance");

  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_concurrency <= kAppMaxConcurrency, "Invalid conc");
  erpc::rt_assert(FLAGS_process_id < FLAGS_num_processes, "Invalid process ID");

  // Physical addrs of msgbufs aren't contiguous across huge pages. I haven't
  // implemented the split copying yet.
  erpc::rt_assert(FLAGS_max_req_size < KB(1900),
                  "Req size too large for IOAT DMA for now");

  // In the IOAT mode, we check pmem file contents after a DMA write completes
  // asynchronously. For correctness, we must prevent data written by copy #i
  // from being overwritten by copy #(i + n), by restricting n
  erpc::rt_assert(
      kAppPmemFileSize / FLAGS_max_req_size >= 2 * FLAGS_concurrency,
      "Concurrency too large. DMA writes will overwrite each other.");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  size_t num_threads = FLAGS_process_id == 0 ? FLAGS_num_proc_0_threads
                                             : FLAGS_num_proc_other_threads;
  std::vector<std::thread> threads(num_threads);

  if (FLAGS_process_id == 0) {
    // Each thread needs at least FLAGS_req_size space in the buffer
    erpc::rt_assert(kAppPmemFileSize >= FLAGS_max_req_size * num_threads);

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
