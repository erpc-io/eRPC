#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::run_event_loop_one() {
  /* First, handle session management events, if any */
  if (unlikely(sm_hook.session_mgmt_ev_counter > 0)) {
    handle_session_management(); /* Callee grabs the hook lock */
  }

  /* Second, check if we need to retransmit any session management requests */
  if (unlikely(mgmt_retry_queue.size() > 0)) {
    mgmt_retry();
  }

  /* Third, process the datapath work queue */
};

}  // End ERpc
