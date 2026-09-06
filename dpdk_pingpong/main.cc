#include "../src/util/hdr_export.h"
// Standalone DPDK RTT baseline: one primary, two pinned polling threads.
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
extern "C" {
#include "hdr_histogram.h"
}

static volatile sig_atomic_t interrupted = 0;
static std::atomic<bool> stop{false}, ready{false}, failed{false};
static void signal_handler(int) { interrupted = 1; }
static void check(bool ok, const char *what) {
  if (!ok) throw std::runtime_error(what);
}
static void check(bool ok, const std::string &what) {
  if (!ok) throw std::runtime_error(what);
}
static void pin(unsigned cpu) {
  check(cpu < CPU_SETSIZE, "CPU exceeds CPU_SETSIZE");
  cpu_set_t mask, actual;
  CPU_ZERO(&mask); CPU_SET(cpu, &mask);
  check(pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) == 0,
        "Cannot pin CPU " + std::to_string(cpu));
  CPU_ZERO(&actual);
  check(pthread_getaffinity_np(pthread_self(), sizeof(actual), &actual) == 0 &&
        CPU_EQUAL(&mask, &actual), "Affinity verification failed");
  printf("Verified worker CPU %u\n", cpu);
}
static unsigned number(const char *s, unsigned low, unsigned high) {
  std::string str(s);
  check(!str.empty() && str.find_first_not_of("0123456789") == std::string::npos,
        "Expected unsigned decimal argument");
  unsigned long n = std::stoul(str);
  check(n >= low && n <= high, "Argument out of range: " + str);
  return static_cast<unsigned>(n);
}
struct Port {
  uint16_t id;
  rte_mempool *pool = nullptr;
  rte_ether_addr mac{};
  bool configured = false;
};
static void setup(Port &p) {
  check(rte_eth_dev_is_valid_port(p.id), "Invalid port " + std::to_string(p.id));
  rte_eth_dev_info info{};
  check(rte_eth_dev_info_get(p.id, &info) == 0 && info.driver_name, "Device info failed");
  bool sfc = std::string(info.driver_name) == "net_sfc_efx";
  int socket = rte_eth_dev_socket_id(p.id);
  if (socket < 0) socket = rte_socket_id();
  printf("Port %u driver=%s socket=%d RX=4096 TX=128 queues=1 loopback=off\n",
         p.id, info.driver_name, socket);
  check(info.rx_desc_lim.nb_max >= 4096 && info.tx_desc_lim.nb_max >= 128,
        "Device cannot support requested ring sizes");
  rte_eth_conf conf{};
  conf.rxmode.mq_mode = ETH_MQ_RX_NONE;
  conf.txmode.mq_mode = ETH_MQ_TX_NONE;
  conf.txmode.offloads = DEV_TX_OFFLOAD_MULTI_SEGS;
  check(rte_eth_dev_configure(p.id, 1, 1, &conf) == 0, "Port configure failed");
  p.configured = true;
  std::string name = "pingpong_pool_" + std::to_string(p.id);
  p.pool = rte_pktmbuf_pool_create(name.c_str(), 8191, 0, 0,
                                  sizeof(rte_mbuf) + RTE_PKTMBUF_HEADROOM + 2048, socket);
  check(p.pool, std::string("Mempool failed: ") + rte_strerror(rte_errno));
  rte_eth_rxconf rx{};
  rx.rx_thresh.pthresh = sfc ? 0 : 8;
  rx.rx_drop_en = sfc ? 1 : 0;
  rte_eth_txconf tx{};
  tx.tx_thresh.pthresh = sfc ? 0 : 32;
  tx.offloads = conf.txmode.offloads;
  check(rte_eth_rx_queue_setup(p.id, 0, 4096, socket, &rx, p.pool) == 0, "RX setup failed");
  check(rte_eth_tx_queue_setup(p.id, 0, 128, socket, &tx) == 0, "TX setup failed");
  check(rte_eth_macaddr_get(p.id, &p.mac) == 0, "MAC query failed");
  check(rte_eth_dev_start(p.id) == 0, "Port start failed");
}
// Match eRPC's 56-byte combined header, including Ethernet/IP/UDP, plus payload.
// The 14 bytes after UDP contain our marker/sequence instead of eRPC metadata.
static std::vector<uint8_t> frame(const Port &src, const Port &dst, unsigned payload) {
  std::vector<uint8_t> bytes(56 + payload, 0);
  auto *eth = reinterpret_cast<rte_ether_hdr *>(bytes.data());
  eth->src_addr = src.mac; eth->dst_addr = dst.mac;
  eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
  auto *ip = reinterpret_cast<rte_ipv4_hdr *>(bytes.data() + 14);
  ip->version_ihl = 0x45;
  ip->total_length = rte_cpu_to_be_16(bytes.size() - 14);
  ip->time_to_live = 64; ip->next_proto_id = 17;
  ip->src_addr = rte_cpu_to_be_32(0x0a000001 + src.id);
  ip->dst_addr = rte_cpu_to_be_32(0x0a000001 + dst.id);
  ip->hdr_checksum = rte_ipv4_cksum(ip);
  auto *udp = reinterpret_cast<rte_udp_hdr *>(bytes.data() + 34);
  udp->src_port = rte_cpu_to_be_16(10000);
  udp->dst_port = rte_cpu_to_be_16(10000);
  udp->dgram_len = rte_cpu_to_be_16(bytes.size() - 34);
  memcpy(bytes.data() + 42, "DPPG", 4);
  return bytes;
}
static void send(Port &p, std::vector<uint8_t> &bytes, uint64_t seq) {
  memcpy(bytes.data() + 46, &seq, sizeof(seq));
  rte_mbuf *m = rte_pktmbuf_alloc(p.pool);
  check(m, "TX mbuf allocation failed");
  void *data = rte_pktmbuf_append(m, bytes.size());
  if (!data) { rte_pktmbuf_free(m); throw std::runtime_error("Frame too large"); }
  memcpy(data, bytes.data(), bytes.size());
  uint64_t deadline = rte_rdtsc() + rte_get_tsc_hz();
  while (rte_eth_tx_burst(p.id, 0, &m, 1) != 1) {
    if (stop || interrupted || rte_rdtsc() > deadline) {
      rte_pktmbuf_free(m);
      throw std::runtime_error("TX stopped or timed out");
    }
  }
}
static bool decode(const rte_mbuf *m, const rte_ether_addr &mac,
                   unsigned length, uint64_t &seq) {
  const uint8_t *data = rte_pktmbuf_mtod(m, const uint8_t *);
  bool valid = length >= 56 && m->nb_segs == 1 && m->data_len == length &&
               memcmp(data, &mac, 6) == 0 && data[12] == 0x08 && data[13] == 0x00 &&
               data[14] == 0x45 && data[23] == 17 &&
               memcmp(data + 42, "DPPG", 4) == 0;
  if (valid) memcpy(&seq, data + 46, sizeof(seq));
  return valid;
}
static bool receive(Port &p, unsigned length, uint64_t &seq) {
  rte_mbuf *m = nullptr;
  if (!rte_eth_rx_burst(p.id, 0, &m, 1)) return false;
  bool valid = decode(m, p.mac, length, seq);
  // Always free the actual mbuf returned by the PMD, regardless of data_off.
  rte_pktmbuf_free(m);
  return valid;
}
static void stats(Port &p) {
  rte_eth_stats s{};
  if (rte_eth_stats_get(p.id, &s) == 0)
    printf("Port %u RX=%lu TX=%lu missed=%lu RX_errors=%lu TX_errors=%lu RX_nombuf=%lu\n",
           p.id, s.ipackets, s.opackets, s.imissed, s.ierrors, s.oerrors, s.rx_nombuf);
}
int main(int argc, char **argv) {
  Port server{0}, client{1};
  bool eal = false;
  std::thread responder;
  hdr_histogram *hist = nullptr;
  try {
    // App arguments follow --, EAL arguments precede it.
    int split = 1;
    while (split < argc && std::string(argv[split]) != "--") ++split;
    check(argc - split == 8 || argc - split == 9,
          "Usage: pingpong EAL_ARGS -- request_bytes warmup_s measure_s server_port client_port server_cpu client_cpu [histogram.csv]");
    unsigned size = number(argv[split+1], 64, 1024);
    unsigned warmup = number(argv[split+2], 0, 600);
    unsigned duration = number(argv[split+3], 1, 3600);
    server.id = number(argv[split+4], 0, 65535);
    client.id = number(argv[split+5], 0, 65535);
    unsigned server_cpu = number(argv[split+6], 0, CPU_SETSIZE-1);
    unsigned client_cpu = number(argv[split+7], 0, CPU_SETSIZE-1);
    check(server.id != client.id && server_cpu != client_cpu, "Ports and worker CPUs must differ");
    const std::string histogram_file = argc - split == 9 ? argv[split+8] : "";
    check(rte_eal_init(split, argv) >= 0, "EAL initialization failed");
    eal = true;
    check(rte_eal_process_type() == RTE_PROC_PRIMARY, "Requires its own primary process");
    signal(SIGINT, signal_handler); signal(SIGTERM, signal_handler);
    setup(server); setup(client);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (true) {
      rte_eth_link a{}, b{};
      check(rte_eth_link_get_nowait(server.id, &a) == 0 &&
            rte_eth_link_get_nowait(client.id, &b) == 0, "Link query failed");
      if (a.link_status && b.link_status) {
        printf("Both links up: server=%u Mbps client=%u Mbps\n", a.link_speed, b.link_speed);
        break;
      }
      check(!interrupted && std::chrono::steady_clock::now() < deadline, "Link wait timed out or interrupted");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    check(hdr_init(1, 100000000, 2, &hist) == 0, "Histogram allocation failed");
    auto request = frame(client, server, size), reply = frame(server, client, 8);
    printf("DPDK-only RTT: request=%u+56=%zu bytes response=8+56=%zu bytes (excluding FCS); one outstanding\n",
           size, request.size(), reply.size());
    responder = std::thread([&] {
      try {
        pin(server_cpu); ready = true;
        while (!stop && !interrupted) {
          uint64_t seq;
          if (receive(server, request.size(), seq)) send(server, reply, seq);
        }
      } catch (const std::exception &e) {
        fprintf(stderr, "Responder: %s\n", e.what()); failed = true; stop = true;
      }
    });
    pin(client_cpu);
    while (!ready && !failed) std::this_thread::yield();
    check(!failed, "Responder startup failed");
    const uint64_t hz = rte_get_tsc_hz();
    uint64_t sequence = 0;
    auto roundtrip = [&] {
      check(!stop && !interrupted, "Run interrupted or responder failed");
      uint64_t seq = ++sequence;
      uint64_t start = rte_rdtsc_precise();
      send(client, request, seq);
      while (true) {
        uint64_t received;
        if (receive(client, reply.size(), received) && received == seq) break;
        check(!stop && !interrupted, "Run interrupted or responder failed");
        check(rte_rdtsc() - start < hz, "No matching response for 1 second (no retransmission in this baseline)");
      }
      return (rte_rdtsc_precise() - start) * 1e6 / hz;
    };
    printf("Warmup: %u seconds\n", warmup);
    uint64_t until = rte_rdtsc() + warmup * hz;
    while (rte_rdtsc() < until) roundtrip();
    uint64_t start = rte_rdtsc(), previous = 0;
    printf("request_bytes median_us p99_us p99.9_us p99.99_us max_us samples elapsed_s\n");
    for (unsigned second = 1; second <= duration; ++second) {
      until = start + second * hz;
      while (rte_rdtsc() < until) {
        double us = roundtrip();
        check(hdr_record_value(hist, std::max<int64_t>(1, static_cast<int64_t>(us * 10))), "Histogram range exceeded");
      }
      check(static_cast<uint64_t>(hist->total_count) > previous, "No measurement progress");
      previous = hist->total_count;
      printf("%u %.1f %.1f %.1f %.1f %.1f %lu %.3f\n", size,
             hdr_value_at_percentile(hist, 50)/10.0, hdr_value_at_percentile(hist, 99)/10.0,
             hdr_value_at_percentile(hist, 99.9)/10.0, hdr_value_at_percentile(hist, 99.99)/10.0,
             hdr_max(hist)/10.0, previous, (rte_rdtsc()-start)*1.0/hz);
      fflush(stdout);
    }
    stop = true; responder.join();
    check(!failed && !interrupted, "Responder failed or interrupted");
    erpc::export_hdr(histogram_file, hist, 10.0,
        "# benchmark=dpdk_pingpong\n# request_bytes=" + std::to_string(size) +
        "\n# response_bytes=8\n# window=1\n# warmup_ms=" + std::to_string(warmup * 1000) +
        "\n# measurement_ms=" + std::to_string(duration * 1000) + "\n");
  } catch (const std::exception &e) {
    fprintf(stderr, "ERROR: %s\n", e.what()); failed = true; stop = true;
    if (responder.joinable()) responder.join();
  }
  if (hist) hdr_close(hist);
  for (Port *p : {&server, &client}) {
    if (p->configured) { stats(*p); rte_eth_dev_stop(p->id); rte_eth_dev_close(p->id); }
    if (p->pool) rte_mempool_free(p->pool);
  }
  if (eal) rte_eal_cleanup();
  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
