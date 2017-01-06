#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

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
  Nexus(uint16_t global_udp_port);
  ~Nexus();

  void register_hook(SessionManagementHook *hook);
  void unregister_hook(SessionManagementHook *hook);

  void install_sigio_handler();
  void session_mgnt_handler();

  /* Hooks into Session Management objects registered by RPC objects */
  std::vector<volatile SessionManagementHook *> reg_hooks;

  /*
   * The UDP port used by all Nexus-es in the cluster to listen on for
   * session management
   */
  const uint16_t global_udp_port;
  int nexus_sock_fd; /* The file descriptor of the UDP socket */
};

static Nexus *nexus_object;
static void sigio_handler(int sig_num) {
  erpc_dprintf("eRPC Nexus: SIGIO handler called with nexus_object = %p\n",
               (void *)nexus_object);
  _unused(sig_num);
  nexus_object->session_mgnt_handler();
}

}  // End ERpc

#endif  // ERPC_RPC_H
