#include <arpa/inet.h>
#include <assert.h>
#include <rte_common.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_ring.h>
#include <rte_version.h>
#include <string>
#include "common.h"

static constexpr size_t kAppMTU = 1024;
static constexpr size_t kAppPortId = 0;

static constexpr size_t kAppNumRingDesc = 256;

static constexpr size_t kAppRxBatchSize = 32;
static constexpr size_t kAppTxBatchSize = 32;

// Maximum number of packet buffers that the memory pool can hold. The
// documentation of `rte_mempool_create` suggests that the optimum value
// (in terms of memory usage) of this number is a power of two minus one.
static constexpr size_t kAppNumMbufs = 8191;
static constexpr size_t kAppNumCacheMbufs = 32;

static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppNumRxQueues = 1;
static constexpr size_t kAppNumTxQueues = 1;

static constexpr size_t kAppRxQueueId = 0;
static constexpr size_t kAppTxQueueId = 0;

static constexpr uint16_t kIPEtherType = 0x800;
static constexpr uint16_t kIPHdrProtocol = 0x11;

uint8_t kDstMAC[6] = {0x3c, 0xfd, 0xfe, 0x56, 0x07, 0x42};
char kDstIP[] = "10.10.1.1";

uint8_t kSrcMAC[6] = {0x3c, 0xfd, 0xfe, 0x56, 0x19, 0x82};
char kSrcIP[] = "10.10.1.2";

// Per-element size for the packet buffer memory pool
static constexpr size_t kAppMbufSize =
    (2048 + static_cast<uint32_t>(sizeof(struct rte_mbuf)) +
     RTE_PKTMBUF_HEADROOM);

struct eth_hdr_t {
  uint8_t dst_mac[6];
  uint8_t src_mac[6];
  uint16_t eth_type;
} __attribute__((packed));

struct ipv4_hdr_t {
  uint8_t ihl : 4;
  uint8_t version : 4;
  uint8_t tos;
  uint16_t tot_len;
  uint16_t id;
  uint16_t frag_off;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t check;
  uint32_t src_ip;
  uint32_t dst_ip;
} __attribute__((packed));

struct udp_hdr_t {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t len;
  uint16_t sum;
} __attribute__((packed));

static constexpr size_t kTotHdrSz =
    sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t);
static_assert(kTotHdrSz == 42, "");

uint32_t ip_from_str(char *ip) {
  uint32_t addr;
  int ret = inet_pton(AF_INET, ip, &addr);
  assert(ret == 1);
  return addr;
}

static uint16_t ip_checksum(ipv4_hdr_t *ipv4_hdr) {
  unsigned long sum = 0;
  const uint16_t *ip1 = reinterpret_cast<uint16_t *>(ipv4_hdr);

  size_t hdr_len = sizeof(ipv4_hdr_t);
  while (hdr_len > 1) {
    sum += *ip1++;
    if (sum & 0x80000000) sum = (sum & 0xFFFF) + (sum >> 16);
    hdr_len -= 2;
  }

  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  return (~sum);
}

void gen_eth_header(eth_hdr_t *eth_header, uint8_t *src_mac, uint8_t *dst_mac) {
  memcpy(eth_header->src_mac, src_mac, 6);
  memcpy(eth_header->dst_mac, dst_mac, 6);
  eth_header->eth_type = htons(kIPEtherType);
}

void gen_ipv4_header(ipv4_hdr_t *ipv4_hdr, uint32_t src_ip, uint32_t dst_ip,
                     uint16_t data_size) {
  ipv4_hdr->version = 4;
  ipv4_hdr->ihl = 5;
  ipv4_hdr->tos = 0;
  ipv4_hdr->tot_len = htons(sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) + data_size);
  ipv4_hdr->id = htons(0);
  ipv4_hdr->frag_off = htons(0);
  ipv4_hdr->ttl = 128;
  ipv4_hdr->protocol = kIPHdrProtocol;
  ipv4_hdr->src_ip = src_ip;
  ipv4_hdr->dst_ip = dst_ip;
  ipv4_hdr->check = ip_checksum(ipv4_hdr);
}

void gen_udp_header(udp_hdr_t *udp_hdr, uint16_t src_port, uint16_t dst_port,
                    uint16_t data_size) {
  udp_hdr->src_port = htons(src_port);
  udp_hdr->dst_port = htons(dst_port);
  udp_hdr->len = htons(sizeof(udp_hdr_t) + data_size);
  udp_hdr->sum = 0;
}

static std::string mac_to_string(const uint8_t *mac) {
  std::ostringstream ret;
  for (size_t i = 0; i < 6; i++) {
    ret << std::hex << static_cast<uint32_t>(mac[i]);
    if (i != 5) ret << ":";
  }
  return ret.str();
}
