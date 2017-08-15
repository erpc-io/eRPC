/**
 * @file rpc_disconnect_handlers.cc
 * @brief Handlers for session management disconnect requests and responses.
 */
#include "rpc.h"

namespace ERpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
template <class TTr>
void Rpc<TTr>::handle_disconnect_req_st(const SmWorkItem &req_wi) {
  assert(in_creator());

  const SmPkt &sm_pkt = req_wi.sm_pkt;
  assert(sm_pkt.pkt_type == SmPktType::kDisconnectReq);

  // Check the info filled by the client
  uint16_t session_num = sm_pkt.server.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec.at(session_num);
  assert(session != nullptr && session->is_server());
  assert(session->is_connected());
  assert(session->server == sm_pkt.server);
  assert(session->client == sm_pkt.client);

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "eRPC Rpc %u: Received disconnect request from %s. Issue",
          rpc_id, sm_pkt.client.name().c_str());

  // Check that responses for all sslots have been sent
  for (const SSlot &sslot : session->sslot_arr) {
    assert(sslot.server_info.req_msgbuf.is_buried());  // Reqs must be buried

    // If there's a response in this sslot, we've finished sending it
    if (sslot.tx_msgbuf != nullptr) {
      assert(sslot.server_info.rfr_rcvd == sslot.tx_msgbuf->num_pkts - 1);
    }
  }

  session->state = SessionState::kDisconnected;  // Temporary state
  free_recvs();

  LOG_INFO("%s. None. Sending response.\n", issue_msg);
  enqueue_sm_resp_st(req_wi, SmErrType::kNoError);

  bury_session_st(session);
}

// We free the session's RECVs before sending the disconnect request, not here
template <class TTr>
void Rpc<TTr>::handle_disconnect_resp_st(const SmPkt &sm_pkt) {
  assert(in_creator());
  assert(sm_pkt.pkt_type == SmPktType::kDisconnectResp);
  assert(sm_pkt.err_type == SmErrType::kNoError);  // Disconnects don't fail

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received disconnect response from %s for session %u. "
          "Issue",
          rpc_id, sm_pkt.server.name().c_str(), sm_pkt.client.session_num);

  // Locate the requester session and do some sanity checks
  uint16_t session_num = sm_pkt.client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];
  assert(session != nullptr && session->is_client());
  assert(session->state == SessionState::kDisconnectInProgress);
  assert(session->client == sm_pkt.client);
  assert(session->server == sm_pkt.server);

  session->state = SessionState::kDisconnected;  // Temporary state

  LOG_INFO("%s: None. Session disconnected.\n", issue_msg);
  sm_handler(session->local_session_num, SmEventType::kDisconnected,
             SmErrType::kNoError, context);
  bury_session_st(session);
}

}  // End ERpc
