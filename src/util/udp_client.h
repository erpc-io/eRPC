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
  UDPClient(const char *addr, int port);
  ~UDPClient();

  ssize_t send(const char *msg, size_t size);

 private:
  int sock_fd;
  int port;
  std::string f_addr;
  struct addrinfo *f_addrinfo;
};

} // End ERpc

#endif
