/**
 * @file interface_addrs.h
 * @brief Functions to retrieve the addresses of IP interfaces
 */

#pragma once

#ifdef __linux__

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

#endif

}  // namespace erpc
