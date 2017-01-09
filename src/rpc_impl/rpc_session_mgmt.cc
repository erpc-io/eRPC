#include "rpc.h"

namespace ERpc {

/**
 * @brief Process all session management events in the queue.
 */
template <class Transport_>
void Rpc<Transport_>::handle_session_management() {
  assert(sm_hook.session_mgmt_ev_counter > 0);
  sm_hook.session_mgmt_mutex.lock();

  /* Handle all session management requests */
  for (SessionMgmtPkt *sm_pkt : sm_hook.session_mgmt_pkt_list) {
    erpc_dprintf("eRPC: RPC %d received session management pkt of type %s\n",
                 app_tid, session_mgmt_pkt_type_str(sm_pkt->pkt_type).c_str());
    free(sm_pkt);
  }

  sm_hook.session_mgmt_pkt_list.clear();
  sm_hook.session_mgmt_ev_counter = 0;
  sm_hook.session_mgmt_mutex.unlock();
};

}  // End ERpc
