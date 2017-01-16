/**
 * @file rpc_connect_handlers.cc
 * @brief Handlers for session management connect requests and responses
 */
#include "rpc.h"
#include <algorithm>

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_req(SessionMgmtPkt *sm_pkt) {
  _unused(sm_pkt);
}

template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_resp(SessionMgmtPkt *sm_pkt) {
  _unused(sm_pkt);
}

}  // End ERpc
