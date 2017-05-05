/**
 * @file rpc_disconnect_handlers.cc
 * @brief Handlers for session management disconnect requests and responses.
 */
#include "rpc.h"

namespace ERpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
template <class TTr>
void Rpc<TTr>::handle_disconnect_req_st(typename Nexus<TTr>::SmWorkItem *wi) {
  assert(in_creator());
  assert(wi != nullptr && wi->epeer != nullptr);

  const SmPkt *sm_pkt = wi->sm_pkt;
  assert(sm_pkt != nullptr && sm_pkt->pkt_type == SmPktType::kDisconnectReq);

  // Check the info filled by the client
  uint16_t session_num = sm_pkt->server.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec.at(session_num);
  assert(session != nullptr && session->is_server());
  assert(session->is_connected());
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  // Create the basic issue message
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc %u: Received disconnect request from %s. Issue",
          rpc_id, sm_pkt->client.name().c_str());

  // Check that responses for all sslots have been sent
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    SSlot &sslot = session->sslot_arr[i];
    assert(sslot.rx_msgbuf.is_buried());  // Request has been buried

    // If there's a response in this sslot, we've finished sending it
    if (sslot.tx_msgbuf != nullptr) {
      assert(sslot.server_info.rfr_rcvd == sslot.tx_msgbuf->num_pkts - 1);
    }
  }

  session->state = SessionState::kDisconnected;
  free_recvs();

  erpc_dprintf("%s. None. Sending response.\n", issue_msg);
  enqueue_sm_resp_st(wi, SmErrType::kNoError);

  bury_session_st(session);  // Free session resources + NULL in session_vec
}

// We free the session's RECVs before sending the disconnect request, not here.
template <class TTr>
void Rpc<TTr>::handle_disconnect_resp_st(SmPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != nullptr);
  assert(sm_pkt->pkt_type == SmPktType::kDisconnectResp);
  assert(sm_pkt->err_type == SmErrType::kNoError);  // Disconnects don't fail

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received disconnect response from %s for session %u. "
          "Issue",
          rpc_id, sm_pkt->server.name().c_str(), sm_pkt->client.session_num);

  // Locate the requester session and do some sanity checks
  uint16_t session_num = sm_pkt->client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];
  assert(session != nullptr && session->is_client());
  assert(session->state == SessionState::kDisconnectInProgress);
  assert(session->client_info.sm_api_req_pending);
  assert(session->client == sm_pkt->client);
  assert(session->server == sm_pkt->server);

  session->client_info.sm_api_req_pending = false;
  session->state = SessionState::kDisconnected;

  if (!session->client_info.sm_callbacks_disabled) {
    erpc_dprintf("%s: None. Session disconnected.\n", issue_msg);
    sm_handler(session->local_session_num, SmEventType::kDisconnected,
               SmErrType::kNoError, context);
  } else {
    erpc_dprintf(
        "%s: None. Session disconnected. Not invoking disconnect "
        "callback because session was never connected successfully.",
        issue_msg);
  }

  bury_session_st(session);  // Free session resources + NULL in session_vec
}

}  // End ERpc
