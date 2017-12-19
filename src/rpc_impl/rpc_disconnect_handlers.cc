/**
 * @file rpc_disconnect_handlers.cc
 * @brief Handlers for session management disconnect requests and responses.
 */
#include "rpc.h"

namespace erpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
template <class TTr>
void Rpc<TTr>::handle_disconnect_req_st(const SmPkt &sm_pkt) {
  assert(in_dispatch());
  assert(sm_pkt.pkt_type == SmPktType::kDisconnectReq &&
         sm_pkt.server.rpc_id == rpc_id);

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "eRPC Rpc %u: Received disconnect request from %s. Issue",
          rpc_id, sm_pkt.client.name().c_str());

  uint16_t session_num = sm_pkt.server.session_num;
  assert(session_num < session_vec.size());

  // Handle reordering. We don't need the session token for this.
  Session *session = session_vec.at(session_num);
  if (session == nullptr) {
    LOG_INFO("%s: Duplicate request. Re-sending response.\n", issue_msg);
    sm_pkt_udp_tx_st(sm_construct_resp(sm_pkt, SmErrType::kNoError));
    return;
  }

  // If we're here, this is the first time we're receiving disconnect request
  assert(session->is_server() && session->is_connected());
  assert(session->server == sm_pkt.server);
  assert(session->client == sm_pkt.client);

  // Responses for all sslots must have been sent
  for (const SSlot &sslot : session->sslot_arr) {
    assert(sslot.server_info.req_msgbuf.is_buried());
    assert(sslot.server_info.req_type == kInvalidReqType);

    // If there's a response in this sslot, we've finished sending it
    if (sslot.tx_msgbuf != nullptr) {
      assert(sslot.server_info.rfr_rcvd == sslot.tx_msgbuf->num_pkts - 1);
    }
  }

  free_ring_entries();

  LOG_INFO("%s. None. Sending response.\n", issue_msg);
  sm_pkt_udp_tx_st(sm_construct_resp(sm_pkt, SmErrType::kNoError));

  bury_session_st(session);
}

// We free the session's ring bufs before sending the disconnect request, so
// not here.
template <class TTr>
void Rpc<TTr>::handle_disconnect_resp_st(const SmPkt &sm_pkt) {
  assert(in_dispatch());
  assert(sm_pkt.pkt_type == SmPktType::kDisconnectResp &&
         sm_pkt.client.rpc_id == rpc_id);
  assert(sm_pkt.err_type == SmErrType::kNoError);  // Disconnects don't fail

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received disconnect response from %s for session %u. "
          "Issue",
          rpc_id, sm_pkt.server.name().c_str(), sm_pkt.client.session_num);

  uint16_t session_num = sm_pkt.client.session_num;
  assert(session_num < session_vec.size());

  // Handle reordering. We don't need the session token for this.
  Session *session = session_vec[session_num];
  if (session == nullptr) {
    LOG_INFO("%s: Duplicate response. Ignoring.\n", issue_msg);
    return;
  }

  assert(session->is_client());
  assert(session->state == SessionState::kDisconnectInProgress);
  assert(session->client == sm_pkt.client);
  assert(session->server == sm_pkt.server);

  LOG_INFO("%s: None. Session disconnected.\n", issue_msg);
  free_ring_entries();  // Free before callback to allow creating a new session
  sm_handler(session->local_session_num, SmEventType::kDisconnected,
             SmErrType::kNoError, context);
  bury_session_st(session);
}

}  // End erpc
