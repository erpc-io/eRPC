/**
 * @file rpc_sm_flush_handlers.cc
 * @brief Handlers for session management flush requests and responses
 */
#include "rpc.h"

namespace ERpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
template <class TTr>
void Rpc<TTr>::handle_sm_flush_req_st(typename Nexus<TTr>::SmWorkItem *wi) {
  assert(in_creator());
  assert(wi != nullptr && wi->epeer != nullptr);

  const SmPkt *sm_pkt = wi->sm_pkt;
  assert(sm_pkt != nullptr && sm_pkt->pkt_type == SmPktType::kFlushCreditReq);

  // Check that the server fields known by the client were filled correctly
  assert(sm_pkt->server.rpc_id == rpc_id);
  assert(strcmp(sm_pkt->server.hostname, nexus->hostname.c_str()) == 0);

  // Create the basic issue message
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc %u: Received disconnect request from %s. Issue",
          rpc_id, sm_pkt->client.name().c_str());

  // Do some sanity checks
  uint16_t session_num = sm_pkt->server.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec.at(session_num);  // The server end point
  assert(session != nullptr);
  assert(session->is_server());
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  // Check that responses for all sslots have been sent
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    SSlot &sslot = session->sslot_arr[i];
    assert(sslot.rx_msgbuf.is_buried());

    if (sslot.tx_msgbuf != nullptr) {
      assert(sslot.tx_msgbuf->pkts_queued == sslot.tx_msgbuf->num_pkts);
    }
  }

  session->state = SessionState::kDisconnected;
  free_recvs();

  erpc_dprintf("%s. None. Sending response.\n", issue_msg);
  enqueue_sm_resp_st(wi, SmErrType::kNoError);

  bury_session_st(session);  // Free session resources + NULL in session_vec
}

template <class TTr>
void Rpc<TTr>::handle_sm_flush_resp_st(SmPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != nullptr);
  assert(sm_pkt->pkt_type == SmPktType::kDisconnectResp);
  assert(sm_pkt->err_type == SmErrType::kNoError);  // Disconnects don't fail
  assert(sm_err_type_is_valid(sm_pkt->err_type));

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
  assert(session != nullptr);
  assert(session->is_client());
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
