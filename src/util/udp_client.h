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
  UDPClient() {
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd == -1) {
      throw std::runtime_error("UDPClient: Failed to create local socket.");
    }
  }

  UDPClient(const UDPClient &) = delete;

  ~UDPClient() {
    for (auto kv : addrinfo_map) freeaddrinfo(kv.second);
    if (sock_fd != -1) close(sock_fd);
  }

  ssize_t send(const std::string rem_hostname, uint16_t rem_port,
               const char *msg, size_t size) {
    struct addrinfo *rem_addrinfo = nullptr;
    if (addrinfo_map.count(rem_hostname) != 0) {
      rem_addrinfo = addrinfo_map.at(rem_hostname);
    } else {
      char port_str[16];
      snprintf(port_str, sizeof(port_str), "%u", rem_port);

      struct addrinfo hints;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      hints.ai_protocol = IPPROTO_UDP;

      int r =
          getaddrinfo(rem_hostname.c_str(), port_str, &hints, &rem_addrinfo);
      if (r != 0 || rem_addrinfo == nullptr) {
        throw std::runtime_error("UDPClient: Failed to resolve remote.");
      }

      addrinfo_map[rem_hostname] = rem_addrinfo;
    }

    ssize_t ret = sendto(sock_fd, msg, size, 0, rem_addrinfo->ai_addr,
                         rem_addrinfo->ai_addrlen);
    if (ret != static_cast<ssize_t>(size)) {
      throw std::runtime_error("sendto() failed. errno = " +
                               std::string(strerror(errno)));
    }

    return ret;
  }

 private:
  int sock_fd = -1;
  std::map<std::string, struct addrinfo *> addrinfo_map;
};

}  // End erpc

#endif  // UDP_CLIENT_H
