/**
 * @file eth_common.h
 * @brief Common definitons for Ethernet-based transports
 */

#pragma once

#include <arpa/inet.h>
#include <assert.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_packet.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sstream>
#include <string>
#include "common.h"

namespace erpc {

static constexpr uint16_t kIPEtherType = 0x800;
static constexpr uint16_t kIPHdrProtocol = 0x11;
static constexpr uint16_t kBaseEthUDPPort = 10000;

static std::string mac_to_string(const uint8_t* mac) {
  std::ostringstream ret;
  for (size_t i = 0; i < 6; i++) {
    ret << std::hex << static_cast<uint32_t>(mac[i]);
    if (i != 5) ret << ":";
  }
  return ret.str();
}

/// Get the network-byte-order IPv4 address from a human-readable IP string
static uint32_t ipv4_from_str(const char* ip) {
  uint32_t addr;
  int ret = inet_pton(AF_INET, ip, &addr);
  rt_assert(ret == 1, "inet_pton() failed for " + std::string(ip));
  return addr;
}

/// Convert a network-byte-order IPv4 address to a human-readable IP string
static std::string ipv4_to_string(uint32_t ipv4_addr) {
  char str[INET_ADDRSTRLEN];
  const char* ret = inet_ntop(AF_INET, &ipv4_addr, str, sizeof(str));
  rt_assert(ret == str, "inet_ntop failed");
  str[INET_ADDRSTRLEN - 1] = 0;  // Null-terminate
  return str;
}

struct eth_hdr_t {
  uint8_t dst_mac_[6];
  uint8_t src_mac_[6];
  uint16_t eth_type_;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[ETH: dst " << mac_to_string(dst_mac_) << ", src "
        << mac_to_string(src_mac_) << ", eth_type "
        << std::to_string(ntohs(eth_type_)) << "]";
    return ret.str();
  }
} __attribute__((packed));

struct ipv4_hdr_t {
  uint8_t ihl_ : 4;
  uint8_t version_ : 4;
  uint8_t ecn_ : 2;
  uint8_t dscp_ : 6;
  uint16_t tot_len_;
  uint16_t id_;
  uint16_t frag_off_;
  uint8_t ttl_;
  uint8_t protocol_;
  uint16_t check_;
  uint32_t src_ip_;
  uint32_t dst_ip_;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[IPv4: ihl " << std::to_string(ihl_) << ", version "
        << std::to_string(version_) << ", ecn " << std::to_string(ecn_)
        << ", tot_len " << std::to_string(ntohs(tot_len_)) << ", id "
        << std::to_string(ntohs(id_)) << ", frag_off "
        << std::to_string(ntohs(frag_off_)) << ", ttl " << std::to_string(ttl_)
        << ", protocol " << std::to_string(protocol_) << ", check "
        << std::to_string(check_) << ", src IP " << ipv4_to_string(src_ip_)
        << ", dst IP " << ipv4_to_string(dst_ip_) << "]";
    return ret.str();
  }
} __attribute__((packed));

struct udp_hdr_t {
  uint16_t src_port_;
  uint16_t dst_port_;
  uint16_t len_;
  uint16_t check_;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[UDP: src_port " << std::to_string(ntohs(src_port_)) << ", dst_port "
        << std::to_string(ntohs(dst_port_)) << ", len "
        << std::to_string(ntohs(len_)) << ", check " << std::to_string(check_)
        << "]";
    return ret.str();
  }
} __attribute__((packed));

static constexpr size_t kInetHdrsTotSize =
    sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t);
static_assert(kInetHdrsTotSize == 42, "");

/// eRPC session endpoint routing info for Ethernet-based transports. The MAC
/// address is in the byte order retrived from the driver. The IPv4 address and
/// UDP port are in host-byte order.
struct eth_routing_info_t {
  uint8_t mac_[6];
  uint32_t ipv4_addr_;            // The IPv4 address for this endpoint
  uint16_t udp_port_;             // The UDP port this endpoint listens on
  uint16_t rxq_id_ = UINT16_MAX;  // The NIC RX queue ID this endpoint listens on

  // Number of entries in this endpoint's NIC RSS indirection table
  uint16_t reta_size_ = UINT16_MAX;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[MAC " << mac_to_string(mac_) << ", IP " << ipv4_to_string(ipv4_addr_)
        << ", UDP port " << std::to_string(udp_port_) << ", RQ queue ID "
        << (rxq_id_ == UINT16_MAX ? " N/A " : std::to_string(rxq_id_))
        << ", RETA size "
        << ((reta_size_ == UINT16_MAX) ? " N/A" : std::to_string(reta_size_))
        << "]";

    return std::string(ret.str());
  }
  // This must be smaller than Transport::kMaxRoutingInfoSize, but a static
  // assert here causes a circular dependency.
};

static std::string frame_header_to_string(const uint8_t* buf) {
  auto* eth_hdr = reinterpret_cast<const eth_hdr_t*>(buf);
  auto* ipv4_hdr = reinterpret_cast<const ipv4_hdr_t*>(&eth_hdr[1]);
  auto* udp_hdr = reinterpret_cast<const udp_hdr_t*>(&ipv4_hdr[1]);

  return eth_hdr->to_string() + ", " + ipv4_hdr->to_string() + ", " +
         udp_hdr->to_string();
}

static void gen_eth_header(eth_hdr_t* eth_header, const uint8_t* src_mac,
                           const uint8_t* dst_mac) {
  memcpy(eth_header->src_mac_, src_mac, 6);
  memcpy(eth_header->dst_mac_, dst_mac, 6);
  eth_header->eth_type_ = htons(kIPEtherType);
}

/// Format the IPv4 header for a UDP packet. All value arguments are in
/// host-byte order. \p data_size is the data payload size in the UDP packet.
static void gen_ipv4_header(ipv4_hdr_t* ipv4_hdr, uint32_t src_ip,
                            uint32_t dst_ip, uint16_t data_size) {
  ipv4_hdr->version_ = 4;
  ipv4_hdr->ihl_ = 5;
  ipv4_hdr->ecn_ = 1;  // ECT => ECN-capable transport
  ipv4_hdr->dscp_ = 0;
  ipv4_hdr->tot_len_ = htons(sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) + data_size);
  ipv4_hdr->id_ = htons(0);
  ipv4_hdr->frag_off_ = htons(0);
  ipv4_hdr->ttl_ = 128;
  ipv4_hdr->protocol_ = kIPHdrProtocol;
  ipv4_hdr->src_ip_ = htonl(src_ip);
  ipv4_hdr->dst_ip_ = htonl(dst_ip);
  ipv4_hdr->check_ = 0;
}

// Compute IP header checksum (copied from DPDK testpmd)
static uint16_t get_ipv4_checksum(const ipv4_hdr_t* ipv4_hdr) {
  auto* ptr16 = reinterpret_cast<const uint16_t*>(ipv4_hdr);
  uint32_t ip_cksum = 0;
  ip_cksum += ptr16[0];
  ip_cksum += ptr16[1];
  ip_cksum += ptr16[2];
  ip_cksum += ptr16[3];
  ip_cksum += ptr16[4];
  ip_cksum += ptr16[6];
  ip_cksum += ptr16[7];
  ip_cksum += ptr16[8];
  ip_cksum += ptr16[9];
  ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) + (ip_cksum & 0x0000FFFF);
  if (ip_cksum > 65535) ip_cksum -= 65535;
  ip_cksum = (~ip_cksum) & 0x0000FFFF;
  if (ip_cksum == 0) ip_cksum = 0xFFFF;

  return static_cast<uint16_t>(ip_cksum);
}

/// Format the UDP header for a UDP packet. All value arguments are in
/// host-byte order. \p data_size is the data payload size in the UDP packet.
static void gen_udp_header(udp_hdr_t* udp_hdr, uint16_t src_port,
                           uint16_t dst_port, uint16_t data_size) {
  udp_hdr->src_port_ = htons(src_port);
  udp_hdr->dst_port_ = htons(dst_port);
  udp_hdr->len_ = htons(sizeof(udp_hdr_t) + data_size);
  udp_hdr->check_ = 0;
}

/// Return the IPv4 address of a kernel-visible interface in host-byte order
static uint32_t get_interface_ipv4_addr(std::string interface) {
  struct ifaddrs *ifaddr, *ifa;
  rt_assert(getifaddrs(&ifaddr) == 0);
  uint32_t ipv4_addr = 0;

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (strcmp(ifa->ifa_name, interface.c_str()) != 0) continue;

    // We might get the same interface multiple times with different sa_family
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }

    auto sin_addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
    ipv4_addr = ntohl(*reinterpret_cast<uint32_t*>(&sin_addr->sin_addr));
  }

  rt_assert(ipv4_addr != 0,
            std::string("Failed to find interface ") + interface);

  freeifaddrs(ifaddr);
  return ipv4_addr;
}

/// Fill the MAC address of kernel-visible interface
static void fill_interface_mac(std::string interface, uint8_t* mac) {
  struct ifreq ifr;
  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  assert(fd >= 0);

  int ret = ioctl(fd, SIOCGIFHWADDR, &ifr);
  rt_assert(ret == 0, "MAC address IOCTL failed");
  close(fd);

  for (size_t i = 0; i < 6; i++) {
    mac[i] = static_cast<uint8_t>(ifr.ifr_hwaddr.sa_data[i]);
  }
}

}  // namespace erpc
