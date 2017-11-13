#ifndef ERPC_UDP_CLIENT_H
#define ERPC_UDP_CLIENT_H

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <map>
#include <stdexcept>

namespace erpc {

/// Basic UDP client class that supports sending messages and caches remote
/// addrinfo mappings
class UDPClient {
 public:
  UDPClient(uint16_t global_udp_port) : global_udp_port(global_udp_port) {
    snprintf(_port_str, sizeof(_port_str), "%u", global_udp_port);

    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd == -1) {
      throw std::runtime_error("UDPClient: Failed to create local socket.");
    }
  }

  UDPClient() {}
  UDPClient(const UDPClient &) = delete;

  ~UDPClient() {
    for (auto kv : addrinfo_map) freeaddrinfo(kv.second);
    if (sock_fd != -1) close(sock_fd);
  }

  ssize_t send(const std::string remote_hostname, const char *msg,
               size_t size) {
    struct addrinfo *remote_addrinfo = nullptr;
    if (addrinfo_map.count(remote_hostname) != 0) {
      remote_addrinfo = addrinfo_map.at(remote_hostname);
    } else {
      struct addrinfo hints;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_DGRAM;
      hints.ai_protocol = IPPROTO_UDP;

      int r = getaddrinfo(remote_hostname.c_str(), _port_str, &hints,
                          &remote_addrinfo);
      if (r != 0 || remote_addrinfo == nullptr) {
        throw std::runtime_error("UDPClient: Failed to resolve remote.");
      }

      addrinfo_map[remote_hostname] = remote_addrinfo;
    }

    return sendto(sock_fd, msg, size, 0, remote_addrinfo->ai_addr,
                  remote_addrinfo->ai_addrlen);
  }

 private:
  uint16_t global_udp_port;
  char _port_str[16];
  int sock_fd = -1;
  std::map<std::string, struct addrinfo *> addrinfo_map;
};

}  // End erpc

#endif  // UDP_CLIENT_H
