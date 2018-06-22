#pragma once

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>

namespace erpc {

/// Basic UDP server class that supports receiving messages
template <class T>
class UDPServer {
 public:
  UDPServer(uint16_t port, size_t timeout_ms, size_t rx_buffer_size = 0)
      : port(port), timeout_ms(timeout_ms) {
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd == -1) {
      throw std::runtime_error("UDPServer: Failed to create local socket.");
    }

    // Set RECV timeout
    if (timeout_ms >= 1000) {
      throw std::runtime_error("UDPServer: Timeout too high.");
    }
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = static_cast<long>(timeout_ms) * 1000;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
      throw std::runtime_error("UDPServer: Failed to set timeout");
    }

    // Set buffer size
    if (rx_buffer_size != 0) {
      int ret = setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &rx_buffer_size,
                           sizeof(rx_buffer_size));
      if (ret != 0) {
        throw std::runtime_error("UDPServer: Failed to set RX buffer size.");
      }
    }

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(static_cast<unsigned short>(port));

    int r = bind(sock_fd, reinterpret_cast<struct sockaddr *>(&serveraddr),
                 sizeof(serveraddr));
    if (r != 0) {
      throw std::runtime_error("UDPServer: Failed to bind socket to port " +
                               std::to_string(port));
    }
  }

  UDPServer() {}
  UDPServer(const UDPServer &) = delete;

  ~UDPServer() {
    if (sock_fd != -1) close(sock_fd);
  }

  ssize_t recv_blocking(T &msg) {
    return recv(sock_fd, static_cast<void *>(&msg), sizeof(T), 0);
  }

 private:
  uint16_t port;  ///< The port to listen on
  size_t timeout_ms;
  int sock_fd = -1;
};

}  // End erpc
