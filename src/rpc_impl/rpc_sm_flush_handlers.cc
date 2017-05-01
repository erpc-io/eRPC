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

  // Check the info filled by the client
  uint16_t session_num = sm_pkt->server.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec.at(session_num);
  assert(session != nullptr && session->is_server());
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  // Create the basic issue message
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received flush credit request from %s. Issue", rpc_id,
          sm_pkt->client.name().c_str());

  if (flush_credits_available == 0) {
    erpc_dprintf("%s: Flush credits exhausted. Sending response.\n", issue_msg);
    enqueue_sm_resp_st(wi, SmErrType::kFlushCreditsExhausted);
  } else {
    flush_credits_available--;
    enqueue_sm_resp_st(wi, SmErrType::kNoError);
  }
}

template <class TTr>
void Rpc<TTr>::handle_sm_flush_resp_st(SmPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != nullptr);
  assert(sm_pkt->pkt_type == SmPktType::kFlushCreditResp);
  assert(sm_pkt->err_type == SmErrType::kNoError ||
         sm_pkt->err_type == SmErrType::kFlushCreditsExhausted);

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received flush credit response from %s for session %u. "
          "Issue",
          rpc_id, sm_pkt->server.name().c_str(), sm_pkt->client.session_num);

  // Locate the requester session and do some sanity checks
  uint16_t session_num = sm_pkt->client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];
  assert(session != nullptr && session->is_client());
  assert(session->state == SessionState::kConnected);
  assert(session->client == sm_pkt->client);
  assert(session->server == sm_pkt->server);

  // XXX: Do something with the response
  if (sm_pkt->err_type == SmErrType::kFlushCreditsExhausted) {
  } else {
  }
}

}  // End ERpc
