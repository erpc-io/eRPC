#ifndef ERPC_UDP_CLIENT_H
#define ERPC_UDP_CLIENT_H

#include <stdlib.h>
#include <string>
#include "util/rand.h"

namespace ERpc {

/**
 * @brief Basic UDP client class that supports sending messages.
 */
class UDPClient {
 public:
  UDPClient(const char *remote_addr, size_t remote_port, double drop_prob);
  ~UDPClient();

  ssize_t send(const char *msg, size_t size);

 private:
  // Constructor args
  std::string remote_addr;
  size_t remote_port;
  double drop_prob;

  // Others
  int sock_fd;
  struct addrinfo *remote_addrinfo;

  SlowRand slow_rand; /* For adding packet drops */
};

}  // End ERpc

#endif  // ERPC_UDP_CLIENT_H
