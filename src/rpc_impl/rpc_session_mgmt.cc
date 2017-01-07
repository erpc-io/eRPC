#include "rpc.h"

namespace ERpc {

/**
 * @brief Process all session management events in the queue. This function
 * is called with the session management hook locked. The caller will unlock
 * the hook when this function returns.
 */
template <class Transport_>
void Rpc<Transport_>::do_session_management() {
  assert(sm_hook.session_mgmt_ev_counter > 0);

  if (sm_hook.session_req_queue.size() > 0) {
    /* Handle a session management request */
  }

  if (sm_hook.session_resp_queue.size() > 0) {
    /* Handle a session management response */
  }
};

}  // End ERpc
