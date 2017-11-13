#ifndef ERPC_UDP_SERVER_H
#define ERPC_UDP_SERVER_H

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>

namespace erpc {

/// Basic UDP server class that supports receiving messages
class UDPServer {
 public:
  UDPServer(uint16_t global_udp_port) : global_udp_port(global_udp_port) {
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_fd == -1) {
      throw std::runtime_error("UDPServer: Failed to create local socket.");
    }

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(static_cast<unsigned short>(global_udp_port));

    int r = bind(sock_fd, reinterpret_cast<struct sockaddr *>(&serveraddr),
                 sizeof(serveraddr));
    if (r != 0) throw std::runtime_error("UDPServer: Failed to bind socket.");
  }

  UDPServer() {}
  ~UDPServer() {
    if (sock_fd != -1) close(sock_fd);
  }

  int recv_blocking(char *msg, size_t max_size) {
    return recv(sock_fd, msg, max_size, 0);
  }

 private:
  uint16_t global_udp_port;
  int sock_fd = -1;
};

}  // End erpc

#endif  // ERPC_UDP_SERVER_H
