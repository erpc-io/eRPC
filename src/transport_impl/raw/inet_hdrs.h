#ifndef ERPC_INET_HDRS_H
#define ERPC_INET_HDRS_H

#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <linux/if_arp.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
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

static std::string mac_to_string(const uint8_t*);  // Foward declaration
static std::string ipv4_to_string(uint32_t);       // Forward declaration

struct eth_hdr_t {
  uint8_t dst_mac[6];
  uint8_t src_mac[6];
  uint16_t eth_type;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[ETH: dst " << mac_to_string(dst_mac) << ", src "
        << mac_to_string(src_mac) << ", eth_type "
        << std::to_string(ntohs(eth_type)) << "]";
    return ret.str();
  }
} __attribute__((packed));

struct ipv4_hdr_t {
  uint8_t ihl : 4;
  uint8_t version : 4;
  uint8_t ecn : 2;
  uint8_t dscp : 6;
  uint16_t tot_len;
  uint16_t id;
  uint16_t frag_off;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t check;
  uint32_t src_ip;
  uint32_t dst_ip;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[IPv4: ihl " << std::to_string(ihl) << ", version "
        << std::to_string(version) << ", ecn " << (ecn == 1 ? "ECT" : "CE")
        << ", tot_len " << std::to_string(ntohs(tot_len)) << ", id "
        << std::to_string(ntohs(id)) << ", frag_off "
        << std::to_string(ntohs(frag_off)) << ", ttl " << std::to_string(ttl)
        << ", protocol " << std::to_string(protocol) << ", check "
        << std::to_string(check) << ", src IP " << ipv4_to_string(src_ip)
        << ", dst IP " << ipv4_to_string(dst_ip) << "]";
    return ret.str();
  }
} __attribute__((packed));

struct udp_hdr_t {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t len;
  uint16_t check;

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[UDP: src_port " << std::to_string(ntohs(src_port)) << ", dst_port "
        << std::to_string(ntohs(dst_port)) << ", len "
        << std::to_string(ntohs(len)) << ", check " << std::to_string(check)
        << "]";
    return ret.str();
  }
} __attribute__((packed));

static constexpr size_t kInetHdrsTotSize =
    sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t);
static_assert(kInetHdrsTotSize == 42, "");

static std::string frame_header_to_string(uint8_t* buf) {
  auto* eth_hdr = reinterpret_cast<eth_hdr_t*>(buf);
  auto* ipv4_hdr = reinterpret_cast<ipv4_hdr_t*>(&eth_hdr[1]);
  auto* udp_hdr = reinterpret_cast<udp_hdr_t*>(&ipv4_hdr[1]);

  return eth_hdr->to_string() + ", " + ipv4_hdr->to_string() + ", " +
         udp_hdr->to_string();
}

static void gen_eth_header(eth_hdr_t* eth_header, const uint8_t* src_mac,
                           const uint8_t* dst_mac) {
  memcpy(eth_header->src_mac, src_mac, 6);
  memcpy(eth_header->dst_mac, dst_mac, 6);
  eth_header->eth_type = htons(kIPEtherType);
}

/// Format the IPv4 header for a UDP packet. Note that \p data_size is the
/// payload size in the UDP packet.
static void gen_ipv4_header(ipv4_hdr_t* ipv4_hdr, uint32_t src_ip,
                            uint32_t dst_ip, uint16_t data_size) {
  ipv4_hdr->version = 4;
  ipv4_hdr->ihl = 5;
  ipv4_hdr->ecn = 1;  // ECN supported
  ipv4_hdr->dscp = 0;
  ipv4_hdr->tot_len = htons(sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) + data_size);
  ipv4_hdr->id = htons(0);
  ipv4_hdr->frag_off = htons(0);
  ipv4_hdr->ttl = 128;
  ipv4_hdr->protocol = kIPHdrProtocol;
  ipv4_hdr->src_ip = src_ip;
  ipv4_hdr->dst_ip = dst_ip;
  ipv4_hdr->check = 0;
}

/// Format the UDP header for a UDP packet. Note that \p data_size is the
/// payload size in the UDP packet.
static void gen_udp_header(udp_hdr_t* udp_hdr, uint16_t src_port,
                           uint16_t dst_port, uint16_t data_size) {
  udp_hdr->src_port = htons(src_port);
  udp_hdr->dst_port = htons(dst_port);
  udp_hdr->len = htons(sizeof(udp_hdr_t) + data_size);
  udp_hdr->check = 0;
}

static std::string mac_to_string(const uint8_t* mac) {
  std::ostringstream ret;
  for (size_t i = 0; i < 6; i++) {
    ret << std::hex << static_cast<uint32_t>(mac[i]);
    if (i != 5) ret << ":";
  }
  return ret.str();
}

static uint32_t ipv4_from_str(const char* ip) {
  uint32_t addr;
  int ret = inet_pton(AF_INET, ip, &addr);
  rt_assert(ret == 1, "inet_pton() failed for " + std::string(ip));
  return addr;
}

static std::string ipv4_to_string(uint32_t ipv4_addr) {
  static_assert(INET_ADDRSTRLEN == 16, "");
  char str[INET_ADDRSTRLEN];
  const char* ret = inet_ntop(AF_INET, &ipv4_addr, str, sizeof(str));
  rt_assert(ret == str, "inet_ntop failed");
  str[INET_ADDRSTRLEN - 1] = 0;  // Null-terminate
  return str;
}

/// Return the IPv4 address of an interface
static uint32_t get_interface_ipv4_addr(std::string interface) {
  struct ifaddrs *ifaddr, *ifa;
  rt_assert(getifaddrs(&ifaddr) == 0);
  uint32_t ipv4_addr = 0;

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr->sa_family != AF_INET) continue;  // IP address
    if (strcmp(ifa->ifa_name, interface.c_str()) != 0) continue;

    auto sin_addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
    ipv4_addr = *reinterpret_cast<uint32_t*>(&sin_addr->sin_addr);
  }

  freeifaddrs(ifaddr);
  return ipv4_addr;
}

/// Fill the MAC address of an interface
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

/// Return the net interface for a verbs device (e.g., mlx5_0 -> enp4s0f0)
static std::string ibdev2netdev(std::string ibdev_name) {
  std::string dev_dir = "/sys/class/infiniband/" + ibdev_name + "/device/net";

  std::vector<std::string> net_ifaces;
  DIR* dp;
  struct dirent* dirp;
  dp = opendir(dev_dir.c_str());
  rt_assert(dp != nullptr, "Failed to open directory " + dev_dir);

  while (true) {
    dirp = readdir(dp);
    if (dirp == nullptr) break;

    if (strcmp(dirp->d_name, ".") == 0) continue;
    if (strcmp(dirp->d_name, "..") == 0) continue;
    net_ifaces.push_back(std::string(dirp->d_name));
  }
  closedir(dp);

  rt_assert(net_ifaces.size() > 0, "Directory " + dev_dir + " is empty");
  return net_ifaces[0];
}

}  // End erpc

#endif  // ERPC_INET_HDRS_H
