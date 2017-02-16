/**
 * @file rpc_connect_handlers.cc
 * @brief Handlers for session management discconnect requests and responses.
 */
#include "rpc.h"
#include <algorithm>

namespace ERpc {

/*
 * We don't need to check remote arguments since the session was already
 * connected successfully.
 */
template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_req(SessionMgmtPkt *sm_pkt) {
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kDisconnectReq);

  /* Ensure that server fields known by the client were filled correctly */
  assert(sm_pkt->server.app_tid == app_tid);
  assert(strcmp(sm_pkt->server.hostname, nexus->hostname) == 0);

  /* Create the basic issue message */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc %s: Received disconnect request from %s. Issue",
          get_name().c_str(), sm_pkt->client.name().c_str());

  uint32_t session_num = sm_pkt->server.session_num;
  assert(session_num < session_vec.size());

  /* Check if the session was already disconnected */
  if (session_vec.at(session_num) == nullptr) {
    erpc_dprintf("%s. Duplicate disconnect request. Sending response.\n",
                 issue_msg);

    sm_pkt->send_resp_mut(SessionMgmtErrType::kNoError, &nexus->udp_config);
    return;
  }

  /*
   * If the session was not already disconnected, the session endpoints
   * (hostname, app TID, session num) in the pkt should match our local copy.
   */
  Session *session = session_vec.at(session_num); /* The server end point */
  assert(session->role == Session::Role::kServer);
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  erpc_dprintf("%s. None. Sending response.\n", issue_msg);
  sm_pkt->send_resp_mut(SessionMgmtErrType::kNoError, &nexus->udp_config);
  bury_session(session);
}

template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_resp(SessionMgmtPkt *sm_pkt) {
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kDisconnectResp);
  assert(session_mgmt_is_valid_err_type(sm_pkt->err_type));

  /* Create the basic issue message using only the packet */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %s: Received disconnect response from %s for "
          "session %u. Issue",
          get_name().c_str(), sm_pkt->server.name().c_str(),
          sm_pkt->client.session_num);

  /* Try to locate the requester session for this response */
  uint32_t session_num = sm_pkt->client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];

  /*
   * Check if the client session was already disconnected. This happens when
   * we get a duplicate disconnect response. If so, the callback is not invoked.
   */
  if (session == nullptr) {
    assert(!mgmt_retry_queue_contains(session));
    erpc_dprintf("%s: Client session is already disconnected.\n", issue_msg);
    return;
  }

  /*
   * If we are here, this is the first disconnect response, so we must be in
   * the kDisconnectInProgress state, and the disconnect request should be in
   * flight. It's not possible to also have a connect request in flight, since
   * the disconnect must wait for the first connect response, at which point
   * the connect response is removed from the in-flight list.
   */
  assert(session->state == SessionState::kDisconnectInProgress);
  assert(mgmt_retry_queue_contains(session));
  mgmt_retry_queue_remove(session);

  /*
   * If the session was not already disconnected, the session endpoints
   * (hostname, app TID, session num) in the pkt should match our local copy.
   */
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  /* Disconnect requests can only succeed */
  assert(sm_pkt->err_type == SessionMgmtErrType::kNoError);

  session->state = SessionState::kDisconnected; /* Mark session connected */

  /* Invoke the handler and bury the session */
  erpc_dprintf("%s: None. Session disconnected.\n", issue_msg);
  session_mgmt_handler(session, SessionMgmtEventType::kDisconnected,
                       SessionMgmtErrType::kNoError, context);
  bury_session(session);
}

}  // End ERpc
