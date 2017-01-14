#ifndef ERPC_UDP_CLIENT_H
#define ERPC_UDP_CLIENT_H

#include <stdlib.h>
#include <string>

namespace ERpc {

/**
 * @brief Basic UDP client class that supports sending messages.
 */
class UDPClient {
 public:
  UDPClient(const char *remote_addr, size_t remote_port);
  ~UDPClient();

  ssize_t send(const char *msg, size_t size);

 private:
  int sock_fd;
  size_t remote_port;
  std::string remote_addr;
  struct addrinfo *remote_addrinfo;
};

}  // End ERpc

#endif  // ERPC_UDP_CLIENT_H
