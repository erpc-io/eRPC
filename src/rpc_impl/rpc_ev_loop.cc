#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::run_event_loop_one() {
  /* First handle any existing session management events */
  if (unlikely(sm_hook.session_mgmt_ev_counter > 0)) {
    handle_session_management(); /* Callee grabs the hook lock */
  }

  /* Then check if we need to retransmit any session management requests */
  if (unlikely(mgmt_retry_queue.size() > 0)) {
    mgmt_retry();
  }
};

}  // End ERpc
