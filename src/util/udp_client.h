#pragma once

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <map>
#include <stdexcept>
#include <vector>

namespace erpc {

/// Basic UDP client class that supports sending messages and caches remote
/// addrinfo mappings
template <class T>
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
               const T &msg) {
    std::string remote_uri = rem_hostname + ":" + std::to_string(rem_port);
    struct addrinfo *rem_addrinfo = nullptr;

    if (addrinfo_map.count(remote_uri) != 0) {
      rem_addrinfo = addrinfo_map.at(remote_uri);
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
        char issue_msg[1000];
        sprintf(issue_msg, "Failed to resolve %s. getaddrinfo error = %s.",
                remote_uri.c_str(), gai_strerror(r));
        throw std::runtime_error(issue_msg);
      }

      addrinfo_map[remote_uri] = rem_addrinfo;
    }

    ssize_t ret = sendto(sock_fd, &msg, sizeof(T), 0, rem_addrinfo->ai_addr,
                         rem_addrinfo->ai_addrlen);
    if (ret != static_cast<ssize_t>(sizeof(T))) {
      throw std::runtime_error("sendto() failed. errno = " +
                               std::string(strerror(errno)));
    }

    if (enable_recording_flag) sent_vec.push_back(msg);
    return ret;
  }

  /// Maintain a all packets sent by this client
  void enable_recording() { enable_recording_flag = true; }

 private:
  int sock_fd = -1;

  /// A cache mapping hostname:udp_port to addrinfo
  std::map<std::string, struct addrinfo *> addrinfo_map;

  /// The list of all packets sent, maintained if recording is enabled
  std::vector<T> sent_vec;
  bool enable_recording_flag = false;  ///< Flag to enable recording for testing
};

}  // namespace erpc
