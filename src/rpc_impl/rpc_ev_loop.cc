#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::run_event_loop() {
  /* First check for session management events */
  if (unlikely(sm_hook.session_mgmt_ev_counter > 0)) {
    handle_session_management(); /* Callee grabs the hook lock */
  }
};

}  // End ERpc
