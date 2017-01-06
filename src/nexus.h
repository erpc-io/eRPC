#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <mutex>
#include <queue>

#include <vector>
#include "common.h"
#include "session.h"
using namespace std;

namespace ERpc {

class Nexus {
 public:
  /**
   * @brief Construct the one-per-process Nexus object
   *
   * @param port The UDP port to listen on for session management
   */
  Nexus(int udp_port);
  ~Nexus();

  void register_hook(SessionManagementHook *hook);
  void unregister_hook(SessionManagementHook *hook);

 private:
  /* Hooks into Session Management objects registered by RPC objects */
  std::vector<volatile SessionManagementHook *> reg_hooks;

  int udp_port;      /* The UDP port to listen on for session management */
  int nexus_sock_fd; /* The file descriptor of the UDP socket */
};

}  // End ERpc

#endif  // ERPC_RPC_H
