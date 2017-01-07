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

  void register_hook(SessionMgmtHook *hook);
  void unregister_hook(SessionMgmtHook *hook);

  void install_sigio_handler();
  void session_mgnt_handler();

  /*
   * The Nexus is shared among all Rpc threads. This lock must be held while
   * calling Nexus functions from Rpc threads.
   */
  std::mutex nexus_lock;

  /* Hooks into session management objects registered by RPC objects */
  std::vector<volatile SessionMgmtHook *> reg_hooks;

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
