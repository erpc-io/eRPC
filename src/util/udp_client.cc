#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>

#include "udp_client.h"

namespace ERpc {

UDPClient::UDPClient(const char *remote_addr, uint16_t remote_port,
                     double drop_prob)
    : remote_addr(remote_addr), remote_port(remote_port), drop_prob(drop_prob) {
  char decimal_port[16];
  snprintf(decimal_port, sizeof(decimal_port), "%u", remote_port);
  decimal_port[sizeof(decimal_port) / sizeof(decimal_port[0]) - 1] = '\0';

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  int r = getaddrinfo(remote_addr, decimal_port, &hints, &remote_addrinfo);
  if (r != 0 || remote_addrinfo == nullptr) {
    printf("UDPClient: Invalid remote address or remote port\n");
    exit(-1);
  }

  sock_fd = socket(remote_addrinfo->ai_family, SOCK_DGRAM, IPPROTO_UDP);
  if (sock_fd == -1) {
    freeaddrinfo(remote_addrinfo);
    printf("UDPClient: Could not create socket\n");
    exit(-1);
  }
}

UDPClient::~UDPClient() {
  freeaddrinfo(remote_addrinfo);
  close(sock_fd);
}

ssize_t UDPClient::send(const char *msg, size_t size) {
  uint64_t rand = slow_rand.next_u64();

  if (rand % 100 >= drop_prob * 100) {
    /* Actually send the packet */
    return sendto(sock_fd, msg, size, 0, remote_addrinfo->ai_addr,
                  remote_addrinfo->ai_addrlen);
  } else {
    /* Pretend as if we sent the packet. This simulates a network drop. */
    return static_cast<ssize_t>(size);
  }
}

}  // End ERpc
